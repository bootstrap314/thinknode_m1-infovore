#!/usr/bin/env python3
"""ThinkNode M1 USB LoRaWAN modem client; emits newline-delimited JSON."""
import argparse
import json
from pathlib import Path
import re
import sys
import time


def parse_event(line):
    """Parse complete firmware records, retaining human-readable diagnostics."""
    kind, _, body = line.partition(' ')
    if kind not in ('STATUS', 'DOWNLINK', 'TX', 'JOIN', 'OK', 'ERR', 'BOOT'):
        return {'type': 'diagnostic', 'message': line}
    event = {'type': kind.lower()}
    for word in body.split():
        if '=' in word:
            key, value = word.split('=', 1)
            event[key] = value
    if kind in ('OK', 'ERR', 'BOOT') or not event.keys() - {'type'}:
        event['message'] = body
    if kind == 'DOWNLINK':
        try:
            payload = event['hex']
            if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,255}', payload):
                raise ValueError()
            if int(event['bytes']) != len(payload) // 2 or not 0 <= int(event['port']) <= 223:
                raise ValueError()
        except (KeyError, ValueError):
            raise ValueError('Malformed downlink record') from None
    return event


class Session:
    def __init__(self, port, emit=lambda event: None, clock=time.monotonic):
        self.port, self.emit, self.clock = port, emit, clock
        self.buffer = bytearray()

    def receive(self, deadline):
        while self.clock() < deadline:
            if b'\n' in self.buffer:
                raw, _, rest = self.buffer.partition(b'\n')
                self.buffer = bytearray(rest)
                if not raw.strip():
                    continue
                if len(raw) > 1024:
                    raise ValueError('Oversized firmware record')
                return parse_event(raw.rstrip(b'\r').decode('ascii', errors='strict'))
            if len(self.buffer) > 1024:
                raise ValueError('Oversized firmware record')
            self.buffer.extend(self.port.read(256))
        raise TimeoutError('No complete response before timeout; transmission outcome may be unknown')

    def command(self, command, kind='ok', timeout=30):
        if not 0 < len(command) <= 95 or any(ord(c) < 32 or ord(c) > 126 or c == ';' for c in command):
            raise ValueError('Invalid or oversized command')
        self.port.write((command + ';\n').encode('ascii'))
        self.port.flush()
        deadline = self.clock() + timeout
        while True:
            event = self.receive(deadline)
            self.emit(event)
            if event['type'] == 'err':
                raise RuntimeError(event['message'])
            terminal = event['type'] == kind and (kind != 'tx' or 'result' in event)
            if terminal:
                if kind == 'tx' and int(event['result']) < 0:
                    raise RuntimeError('Radio transmission failed; see TX result')
                if kind == 'join' and event.get('joined') != '1':
                    raise RuntimeError('LoRaWAN join failed; check gateway, server and credentials')
                return event

    def handshake(self):
        status = self.command('status', 'status')
        if status.get('protocol') != '1':
            raise RuntimeError('This client requires native M1 firmware with protocol=1')
        return status


def provisioning(path):
    try:
        data = json.loads(Path(path).read_text())
        values = [data[k] for k in ('join_eui', 'dev_eui', 'app_key')]
        if not all(isinstance(v, str) and re.fullmatch('[0-9a-fA-F]{%d}' % n, v)
                   for v, n in zip(values, (16, 16, 32))):
            raise ValueError()
        if int(values[1], 16) == 0 or int(values[2], 16) == 0:
            raise ValueError()
    except (KeyError, ValueError, TypeError):
        raise ValueError('Credential file needs join_eui (16 hex), dev_eui (16 hex), app_key (32 hex)') from None
    return 'provision ' + ' '.join(values)


def emit(event):
    print(json.dumps(event, sort_keys=True), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Explicit application serial port, e.g. /dev/cu.usbmodemXXXX or COM3')
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('ports', 'status', 'join', 'poll'):
        commands.add_parser(name)
    mode = commands.add_parser('mode')
    mode.add_argument('mode', choices=('tracker', 'dongle'))
    send = commands.add_parser('send')
    send.add_argument('fport', type=int, choices=range(2, 224), metavar='FPORT')
    send.add_argument('hex', help='1–32 bytes as hexadecimal')
    provision = commands.add_parser('provision')
    provision.add_argument('--file', required=True, help='Private JSON file; keys are never printed')
    monitor = commands.add_parser('monitor')
    monitor.add_argument('--interval', type=int, choices=range(2, 61), default=5, metavar='SECONDS')
    args = parser.parse_args()
    if args.command != 'ports' and not args.port:
        parser.error('--port is required')
    if args.command == 'send' and not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,32}', args.hex):
        parser.error('send needs 1–32 bytes of even-length hexadecimal')
    # Validate private input before opening the device. Never echo it into logs.
    provision_command = provisioning(args.file) if args.command == 'provision' else None
    import serial
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == 0x239A and p.pid == 0x4405]
    if args.command == 'ports':
        for port in ports:
            emit({'type': 'port', 'port': port.device, 'serial': port.serial_number})
        return
    if not any(p.device == args.port for p in ports):
        raise RuntimeError('Port is not an M1 application USB identity; bootloader ports cannot send LoRaWAN')
    with serial.Serial(args.port, 115200, timeout=.1, write_timeout=2) as port:
        port.dtr = True
        session = Session(port, emit)
        session.handshake()
        if args.command == 'status':
            return
        if args.command == 'provision':
            session.command(provision_command)
        elif args.command == 'mode':
            session.command('mode ' + args.mode)
        elif args.command == 'join':
            session.command('join', 'join')
        elif args.command == 'send':
            session.command(f'send {args.fport} {args.hex}', 'tx')
        elif args.command == 'poll':
            session.command('poll', 'tx')
        elif args.command == 'monitor':
            session.command('stream ' + str(args.interval))
            try:
                while True:
                    emit(session.receive(time.monotonic() + args.interval + 30))
            finally:
                try:
                    session.command('stream off', timeout=2)
                except (OSError, RuntimeError, ValueError, TimeoutError):
                    pass


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
    except (OSError, ImportError, RuntimeError, ValueError, TimeoutError) as error:
        print(json.dumps({'type': 'error', 'message': str(error)}), file=sys.stderr)
        sys.exit(1)
