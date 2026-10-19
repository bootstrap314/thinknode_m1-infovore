#!/usr/bin/env python3
"""Verify first, then serial-DFU one explicitly selected ThinkNode M1."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import time
from verify import verify, source_digest
from bootstrap import ROOT
import shutil
try:
    from termios import error as TermiosError
except ImportError: # Non-POSIX hosts do not have termios.
    TermiosError = OSError


def touch_legacy_port(port, serial_error):
    """Native firmware may disconnect before the legacy 1200-baud fallback."""
    try:
        port.baudrate = 1200
        port.dtr = False
    except (serial_error, OSError, TermiosError):
        pass # Upload still requires the matching bootloader to enumerate below.


def serial_upload(tool, package, port):
    # adafruit-nrfutil 0.5.x can print an upload exception and still exit zero.
    # Require its explicit success acknowledgement as well as a clean exit.
    result = subprocess.run([str(tool), 'dfu', 'serial', '--package', str(package),
                             '--port', port, '--baudrate', '115200'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=90, check=False)
    print(result.stdout, end='', flush=True)
    if (result.returncode != 0 or 'Device programmed.' not in result.stdout.splitlines()
            or 'Failed to upgrade target.' in result.stdout):
        raise RuntimeError('Serial DFU did not confirm successful programming; see upload output above')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtos', choices=('zephyr', 'freertos'), required=True)
    parser.add_argument('--port', help='M1 application or bootloader serial port')
    parser.add_argument('--check-only', action='store_true', help='Run the complete gate without accessing USB')
    args = parser.parse_args()
    if not args.check_only and not args.port:
        parser.error('--port is required to flash (or use --check-only)')
    report = verify(build=True, rtos=args.rtos) # Must succeed before importing serial or opening any device.
    if args.check_only:
        return
    import serial
    import serial.tools.list_ports
    tool = shutil.which('adafruit-nrfutil')
    if not tool:
        raise RuntimeError('Install the optional adafruit-nrfutil serial bootloader tool; see docs/FLASHING.md')
    ports = list(serial.tools.list_ports.comports())
    selected = next((p for p in ports if p.device == args.port), None)
    if not selected or selected.vid != 0x239A or selected.pid not in (0x4405, 0x00DA) or not selected.serial_number:
        raise RuntimeError('Selected port is not a recognized ThinkNode M1 USB identity')
    serial_id = selected.serial_number.upper()
    package = ROOT / 'dist' / args.rtos / ('thinknode-m1-' + args.rtos + '-dfu.zip')
    if source_digest() != report['source_sha256'] or hashlib.sha256(package.read_bytes()).hexdigest() != report['firmware'][args.rtos]['dfu_sha256']:
        raise RuntimeError('Source or firmware changed after verification')
    if selected.pid == 0x4405:
        with serial.Serial(selected.device, 115200, timeout=.2) as port:
            port.dtr = True
            time.sleep(.3)
            port.write(b'bootloader;\n')
            port.flush()
            time.sleep(.3)
            # Legacy firmware uses the 1200-baud touch. The native firmware
            # also supports an explicit bootloader command over USB/BLE.
            touch_legacy_port(port, serial.SerialException)
    deadline = time.monotonic() + 15
    boot = None
    while time.monotonic() < deadline:
        boot = next((p for p in serial.tools.list_ports.comports()
                     if (p.serial_number or "").upper() == serial_id and p.vid == 0x239A and p.pid == 0x00DA), None)
        if boot: break
        time.sleep(.25)
    if not boot:
        raise RuntimeError('Matching M1 serial bootloader did not appear; no upload attempted')
    # Recheck immediately before upload, including edits made while entering DFU.
    if source_digest() != report['source_sha256'] or hashlib.sha256(package.read_bytes()).hexdigest() != report['firmware'][args.rtos]['dfu_sha256']:
        raise RuntimeError('Source or firmware changed before upload; run verification again')
    serial_upload(tool, package, boot.device)
    print('Upload complete. Verify startup and phone pairing before disconnecting.')

if __name__ == '__main__':
    try:
        main()
    except (ImportError, OSError, TermiosError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        print('FLASH ABORTED: ' + str(error), file=sys.stderr)
        sys.exit(1)
