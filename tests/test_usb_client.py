import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import m1


class Wire:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.writes = []
    def read(self, size):
        return self.chunks.pop(0) if self.chunks else b''
    def write(self, data):
        self.writes.append(data)
    def flush(self):
        pass


class Clock:
    def __init__(self): self.value = 0
    def __call__(self):
        self.value += .01
        return self.value


class UsbClient(unittest.TestCase):
    def session(self, chunks):
        wire = Wire(chunks)
        events = []
        return m1.Session(wire, events.append, Clock()), wire, events

    def test_fragmented_and_coalesced_downlink_during_tx(self):
        session, wire, events = self.session([
            b'TX starting; remote execution is not confirmed\r\nDOWN',
            b'LINK port=10 bytes=3 hex=0102', b'03\r', b'\nTX result=1\r\n'])
        self.assertEqual(session.command('send 10 010203', 'tx')['result'], '1')
        self.assertEqual(events[1], {'type': 'downlink', 'port': '10', 'bytes': '3', 'hex': '010203'})
        self.assertEqual(wire.writes, [b'send 10 010203;\n'])

    def test_handshake_requires_native_protocol(self):
        session, _, _ = self.session([b'STATUS rtos=legacy\n'])
        with self.assertRaisesRegex(RuntimeError, 'protocol=1'): session.handshake()
        session, _, _ = self.session([b'BOOT ThinkNode-M1\r\nSTATUS protocol=1 mode=dongle\r\n'])
        self.assertEqual(session.handshake()['mode'], 'dongle')

    def test_error_does_not_retry_radio(self):
        session, wire, _ = self.session([b'ERR radio busy; retry after seconds=60\n'])
        with self.assertRaisesRegex(RuntimeError, 'radio busy'): session.command('poll', 'tx')
        self.assertEqual(len(wire.writes), 1)

    def test_timeout_and_oversized_record(self):
        session, wire, _ = self.session([b'TX result='])
        with self.assertRaises(TimeoutError): session.command('poll', 'tx', timeout=.1)
        self.assertEqual(len(wire.writes), 1)
        session, _, _ = self.session([b'x'*1025])
        with self.assertRaisesRegex(ValueError, 'Oversized'): session.command('status', 'status')

    def test_invalid_commands_never_written(self):
        for cmd in ('', 'status;bootloader', 'status\n', 'x'*96):
            session, wire, _ = self.session([])
            with self.assertRaises(ValueError): session.command(cmd)
            self.assertEqual(wire.writes, [])

    def test_malformed_downlinks_and_radio_failures(self):
        for line in ('DOWNLINK port=10 bytes=2 hex=00', 'DOWNLINK port=999 bytes=1 hex=00',
                     'DOWNLINK port=10 bytes=1 hex=gg'):
            with self.assertRaises(ValueError): m1.parse_event(line)
        for text, kind in ((b'JOIN joined=0 result=-1116\n', 'join'), (b'TX result=-1\n', 'tx')):
            session, _, _ = self.session([text])
            with self.assertRaises(RuntimeError): session.command('join' if kind == 'join' else 'poll', kind)

    def test_private_file_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'keys.json'
            keys = {'join_eui': '0'*16, 'dev_eui': '1'*16, 'app_key': '2'*32}
            path.write_text(json.dumps(keys))
            self.assertEqual(m1.provisioning(path), 'provision ' + ' '.join(keys.values()))
            keys['app_key'] = 'private-invalid-value'
            path.write_text(json.dumps(keys))
            with self.assertRaises(ValueError) as caught: m1.provisioning(path)
            self.assertNotIn(keys['app_key'], str(caught.exception))
