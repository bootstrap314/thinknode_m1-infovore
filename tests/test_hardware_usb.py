"""Do not report a hardware pass for the wrong RTOS or firmware version."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import hardware_usb


class HardwareIdentity(unittest.TestCase):
    def test_wrong_rtos_is_rejected_before_commands(self):
        for expected, actual in [('zephyr', 'freertos'), ('freertos', 'zephyr')]:
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as tmp, \
                 patch.object(hardware_usb.list_ports, 'comports', return_value=[
                     SimpleNamespace(device='selected', vid=0x239A, pid=0x4405, serial_number='test')]), \
                 patch.object(hardware_usb.serial, 'Serial'), \
                 patch.object(hardware_usb, 'Session') as session, \
                 contextlib.redirect_stdout(io.StringIO()):
                session.return_value.handshake.return_value = {'rtos': actual, 'version': 'dev'}
                report = Path(tmp) / 'report.json'
                with self.assertRaisesRegex(RuntimeError, 'protocol handshake'):
                    hardware_usb.test('selected', report, expected)
                session.return_value.command.assert_not_called()
                self.assertFalse(json.loads(report.read_text())['usb_passed'])

    def test_wrong_manifest_is_rejected_before_opening_usb(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(hardware_usb.serial, 'Serial') as port:
            manifest = Path(tmp) / 'manifest.json'
            manifest.write_text(json.dumps({'rtos': 'zephyr', 'version': 'dev'}))
            with self.assertRaisesRegex(ValueError, 'Manifest does not match'):
                hardware_usb.test('selected', Path(tmp) / 'report.json', 'freertos', manifest)
            port.assert_not_called()

    def test_wrong_version_is_rejected_before_commands(self):
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(hardware_usb.list_ports, 'comports', return_value=[
                 SimpleNamespace(device='selected', vid=0x239A, pid=0x4405, serial_number='test')]), \
             patch.object(hardware_usb.serial, 'Serial'), \
             patch.object(hardware_usb, 'Session') as session, \
             contextlib.redirect_stdout(io.StringIO()):
            session.return_value.handshake.return_value = {'rtos': 'freertos', 'version': 'dev'}
            manifest = Path(tmp) / 'manifest.json'
            manifest.write_text(json.dumps({'rtos': 'freertos', 'version': 'v0.1.0-rc.1'}))
            report = Path(tmp) / 'report.json'
            with self.assertRaisesRegex(RuntimeError, 'version matches supplied manifest'):
                hardware_usb.test('selected', report, 'freertos', manifest)
            session.return_value.command.assert_not_called()
            self.assertFalse(json.loads(report.read_text())['usb_passed'])


if __name__ == '__main__':
    unittest.main()
