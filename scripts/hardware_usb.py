#!/usr/bin/env python3
"""Opt-in USB acceptance test for a flashed M1; does not provision keys or transmit RF."""
import argparse
import json
from pathlib import Path
import sys
import time
import serial
from serial.tools import list_ports
from m1 import Session


def test(port_name, report_path, rtos='zephyr', manifest_path=None):
    if rtos not in ('zephyr', 'freertos'):
        raise ValueError('Select zephyr or freertos')
    manifest = json.loads(Path(manifest_path).read_text()) if manifest_path else None
    if manifest is not None and (manifest.get('rtos') != rtos or not manifest.get('version')):
        raise ValueError('Manifest does not match the selected RTOS or has no version')
    port_info = next((p for p in list_ports.comports() if p.device == port_name), None)
    if not port_info or (port_info.vid, port_info.pid) != (0x239A, 0x4405):
        raise RuntimeError('Select an M1 application USB port')
    report = {'usb_passed': False, 'serial': port_info.serial_number, 'expected_rtos': rtos,
              'bluetooth_pairing': 'not tested by this script', 'lorawan': 'not tested', 'events': [], 'checks': []}
    def emit(event):
        report['events'].append(event)
        print(json.dumps(event), flush=True)
    def check(condition, name):
        if not condition: raise RuntimeError('FAILED: ' + name)
        report['checks'].append(name)
        print('PASS: ' + name, flush=True)
    try:
        with serial.Serial(port_name, 115200, timeout=.1, write_timeout=2) as port:
            port.dtr = True
            session = Session(port, emit)
            initial = session.handshake()
            report['rtos'], report['version'] = initial['rtos'], initial['version']
            check(initial['rtos'] == rtos, ('Zephyr' if rtos == 'zephyr' else 'FreeRTOS') + ' protocol handshake')
            if manifest is not None:
                check(initial['version'] == manifest['version'], 'Firmware version matches supplied manifest')
            session.command('stream off')
            session.command('mode dongle')
            status = session.command('status', 'status')
            check(status['mode'] == 'dongle' and status['gps_power'] != 'awake', 'Dongle mode and GPS standby')
            voltage = float(status['battery'].removesuffix('V'))
            check(2.5 <= voltage <= 4.5, 'Plausible battery voltage')
            session.command('light on')
            check(session.command('status', 'status')['light'] == 'on', 'Backlight enable')
            session.command('light off')
            check(session.command('status', 'status')['light'] == 'off', 'Backlight disable')
            if initial['light'] == 'on': session.command('light on')
            # Only verify rejection when there is no active LoRaWAN session.
            if status['lora'] == 'not_joined':
                try:
                    session.command('send 10 01', 'tx')
                except RuntimeError as error:
                    check('not joined' in str(error), 'Unjoined send rejected without RF')
                else: raise RuntimeError('Unjoined send unexpectedly accepted')
            port.write(b'sta'); port.flush()
            try:
                session.receive(time.monotonic()+.25)
            except TimeoutError:
                pass
            else: raise RuntimeError('Partial command was executed')
            port.write(b'tus;\n'); port.flush()
            check(session.receive(time.monotonic()+3)['type'] == 'status', 'Split USB command framing')
            port.write(b'x'*96+b';\n'); port.flush()
            check(session.receive(time.monotonic()+3)['type'] == 'err', 'Oversized command rejected')
            check(session.command('status','status')['protocol']=='1', 'Parser recovers after invalid input')
            session.command('stream 2')
            statuses=[]
            deadline=time.monotonic()+8
            while len(statuses)<4:
                event=session.receive(deadline);emit(event)
                if event['type']=='status':statuses.append(event)
            check(all(s['mode']=='dongle' for s in statuses), 'USB status streaming')
            session.command('stream off')
            session.command('stream 2')
            # Closing the port must stop this subscription; mode remains selected.
        time.sleep(.4)
        with serial.Serial(port_name,115200,timeout=.1,write_timeout=2) as port:
            port.dtr=True
            session=Session(port,emit)
            check(session.handshake()['mode']=='dongle','Serial close/reopen retains dongle mode')
            try:
                event=session.receive(time.monotonic()+3)
            except TimeoutError:
                check(True,'Closing USB stops its status subscription')
            else:
                raise RuntimeError('Unexpected event after reopening: '+event['type'])
        report['usb_passed']=True
        # Associate only an explicitly supplied, version-checked manifest. A local
        # dist folder may describe a different build from the one on the device.
        if manifest is not None: report['firmware_manifest'] = manifest
        return report
    finally:
        report_path.parent.mkdir(parents=True,exist_ok=True)
        report_path.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--rtos',choices=('zephyr','freertos'),default='zephyr')
    parser.add_argument('--manifest',type=Path,help='Optional manifest for the installed image; checks RTOS and version')
    parser.add_argument('--report',type=Path,default=Path('build/hardware/usb-report.json'))
    args=parser.parse_args()
    try:test(args.port,args.report,args.rtos,args.manifest)
    except (OSError,ValueError,RuntimeError,TimeoutError) as error:
        print('HARDWARE TEST FAILED: '+str(error),file=sys.stderr);sys.exit(1)
