import binascii
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from firmware_artifacts import validate, APP_START, APP_END, FAMILY

class Artifacts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.uf2 = self.root / 'firmware.uf2'
        self.zip = self.root / 'firmware.zip'
        self.binary = bytes(range(256))
        self.block = bytearray(512)
        struct.pack_into('<8I', self.block, 0, 0x0A324655, 0x9E5D5157, 0x2000, APP_START, 256, 0, 1, FAMILY)
        self.block[32:288] = self.binary
        struct.pack_into('<I', self.block, 508, 0x0AB16F30)
        self.uf2.write_bytes(self.block)
        self.package()
    def package(self, binary=None, extra=None):
        init = {'device_type': 82, 'device_revision': 65535, 'application_version': 4294967295,
                'softdevice_req': [0xB6], 'firmware_crc16': binascii.crc_hqx(self.binary, 0xFFFF)}
        manifest = {'application': {'bin_file': 'app.bin', 'dat_file': 'app.dat',
                                    'init_packet_data': init}, 'dfu_version': 0.5}
        if extra: manifest.update(extra)
        with zipfile.ZipFile(self.zip, 'w') as z:
            z.writestr('manifest.json', json.dumps({'manifest': manifest}))
            z.writestr('app.bin', self.binary if binary is None else binary)
            z.writestr('app.dat', struct.pack('<HHIHHH', 82, 65535, 4294967295, 1, 0xB6, init['firmware_crc16']))
    def test_valid_application(self):
        self.assertEqual(validate(self.uf2, self.zip)['application_start'], '0x26000')
    def test_protected_flash_regions(self):
        for address in (0, APP_START - 256, APP_END):
            struct.pack_into('<I', self.block, 12, address)
            self.uf2.write_bytes(self.block)
            with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_wrong_family(self):
        struct.pack_into('<I', self.block, 28, 123)
        self.uf2.write_bytes(self.block)
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_mismatched_images(self):
        self.package(binary=b'x' * 256)
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_bootloader_package_rejected(self):
        self.package(extra={'bootloader': {}})
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_corrupt_magic(self):
        self.block[0] = 0
        self.uf2.write_bytes(self.block)
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_corrupt_initialization_packet(self):
        with zipfile.ZipFile(self.zip) as z:
            members = {name: z.read(name) for name in z.namelist()}
        members['app.dat'] = b'corrupt'
        with zipfile.ZipFile(self.zip, 'w') as z:
            for name, data in members.items(): z.writestr(name, data)
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)
    def test_duplicate_blocks(self):
        struct.pack_into('<I', self.block, 24, 2)
        self.uf2.write_bytes(self.block * 2)
        with self.assertRaises(ValueError): validate(self.uf2, self.zip)

if __name__ == '__main__': unittest.main()
