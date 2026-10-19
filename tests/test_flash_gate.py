"""Check that the supported flash entry point cannot upload after failed checks."""
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import flash

class FlashGate(unittest.TestCase):
    def test_failed_verification_prevents_usb_and_upload(self):
        with patch.object(sys, 'argv', ['flash.py', '--rtos', 'zephyr', '--port', '/dev/not-a-device']), \
             patch.object(flash, 'verify', side_effect=RuntimeError('simulated failed test')) as gate, \
             patch.object(flash.subprocess, 'run') as upload, \
             patch.dict(sys.modules, {'serial': None}):
            with self.assertRaisesRegex(RuntimeError, 'simulated failed test'): flash.main()
            gate.assert_called_once_with(build=True, rtos='zephyr')
            upload.assert_not_called()
    def test_check_only_does_not_import_serial_or_upload(self):
        with patch.object(sys, 'argv', ['flash.py', '--rtos', 'freertos', '--check-only']), \
             patch.object(flash, 'verify', return_value={'passed': True}) as gate, \
             patch.object(flash.subprocess, 'run') as upload, \
             patch.dict(sys.modules, {'serial': None}):
            flash.main()
            gate.assert_called_once_with(build=True, rtos='freertos')
            upload.assert_not_called()
    def test_legacy_touch_sets_baud_and_dtr(self):
        class Port:
            baudrate = 115200
            dtr = True
        port = Port()
        flash.touch_legacy_port(port, OSError)
        self.assertEqual(port.baudrate, 1200)
        self.assertFalse(port.dtr)

    def test_native_disconnect_during_legacy_touch_is_tolerated(self):
        class Port:
            @property
            def baudrate(self): return 115200
            @baudrate.setter
            def baudrate(self, value): raise flash.TermiosError(6, 'Device not configured')
            @property
            def dtr(self): raise AssertionError('Do not continue touching a disconnected port')
            @dtr.setter
            def dtr(self, value): raise AssertionError('Do not continue touching a disconnected port')
        flash.touch_legacy_port(Port(), OSError)

    def test_legacy_touch_does_not_hide_programming_errors(self):
        class Port:
            @property
            def baudrate(self): return 115200
            @baudrate.setter
            def baudrate(self, value): raise TypeError('bad value')
        with self.assertRaises(TypeError): flash.touch_legacy_port(Port(), OSError)

    def test_serial_upload_requires_success_acknowledgement(self):
        result = flash.subprocess.CompletedProcess([], 0, 'Activating new firmware\nDevice programmed.\n')
        with patch.object(flash.subprocess, 'run', return_value=result) as run, contextlib.redirect_stdout(io.StringIO()):
            flash.serial_upload('uploader', 'image.zip', 'selected-port')
            self.assertEqual(run.call_args.args[0][-3:], ['selected-port', '--baudrate', '115200'])
            self.assertEqual(run.call_args.kwargs['timeout'], 90)

    def test_serial_upload_rejects_failure_with_zero_exit(self):
        result = flash.subprocess.CompletedProcess([], 0, 'Failed to upgrade target. Error is: disconnected\n')
        with patch.object(flash.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, 'did not confirm successful programming'):
                flash.serial_upload('uploader', 'image.zip', 'selected-port')

    def test_serial_upload_rejects_ambiguous_zero_exit(self):
        result = flash.subprocess.CompletedProcess([], 0, 'Activating new firmware\n')
        with patch.object(flash.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(RuntimeError): flash.serial_upload('uploader', 'image.zip', 'selected-port')

    def test_serial_upload_rejects_errors_even_with_acknowledgement(self):
        for code, output in [(1, 'Device programmed.\n'),
                             (0, 'Failed to upgrade target.\nDevice programmed.\n')]:
            with self.subTest(code=code, output=output):
                result = flash.subprocess.CompletedProcess([], code, output)
                with patch.object(flash.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises(RuntimeError): flash.serial_upload('uploader', 'image.zip', 'selected-port')

    def test_flash_requires_explicit_port(self):
        with patch.object(sys, 'argv', ['flash.py']), patch.object(flash, 'verify') as gate, \
             contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error: flash.main()
            self.assertEqual(error.exception.code, 2)
            gate.assert_not_called()

if __name__ == '__main__': unittest.main()
