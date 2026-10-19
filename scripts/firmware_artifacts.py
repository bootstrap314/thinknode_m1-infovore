"""Validate an application-only nRF52840 UF2/serial-DFU pair before flashing."""
import binascii
import hashlib
import json
import struct
import zipfile
from pathlib import Path

APP_START, APP_END, FAMILY = 0x26000, 0xE5000, 0xADA52840

def validate(uf2_path, zip_path):
    uf2_path, zip_path = Path(uf2_path), Path(zip_path)
    blob = uf2_path.read_bytes()
    if not blob or len(blob) % 512:
        raise ValueError('UF2 size must be a positive multiple of 512')
    blocks = {}
    count = len(blob) // 512
    for offset in range(0, len(blob), 512):
        a, b, flags, address, length, index, total, family = struct.unpack_from('<8I', blob, offset)
        if (a, b) != (0x0A324655, 0x9E5D5157) or struct.unpack_from('<I', blob, offset + 508)[0] != 0x0AB16F30:
            raise ValueError('Invalid UF2 magic')
        if flags != 0x2000 or family != FAMILY:
            raise ValueError('Wrong UF2 flags or nRF52840 family')
        if total != count or index >= count or index in blocks:
            raise ValueError('Invalid or duplicate UF2 block index')
        if length != 256 or not APP_START <= address < address + length <= APP_END:
            raise ValueError('UF2 writes outside the application region or has unexpected payload size')
        blocks[index] = (address, blob[offset + 32:offset + 32 + length])
    ordered = [blocks[i] for i in range(count)]
    for i, (address, _) in enumerate(ordered):
        if address != APP_START + i * 256:
            raise ValueError('UF2 application blocks are not contiguous from 0x26000')
    image = b''.join(data for _, data in ordered)
    with zipfile.ZipFile(zip_path) as package:
        names = package.namelist()
        if len(names) != len(set(names)) or 'manifest.json' not in names:
            raise ValueError('Invalid DFU file manifest')
        manifest = json.loads(package.read('manifest.json'))['manifest']
        if set(manifest) - {'application', 'dfu_version'} or 'application' not in manifest:
            raise ValueError('DFU package must contain application only')
        app = manifest['application']
        binary_name, dat_name = app['bin_file'], app['dat_file']
        if set(names) != {'manifest.json', binary_name, dat_name}:
            raise ValueError('Unexpected DFU package members')
        binary = package.read(binary_name)
        if not binary or binary != image[:len(binary)] or len(image) - len(binary) not in range(256):
            raise ValueError('DFU binary differs from the verified UF2 image')
        init = app.get('init_packet_data', {})
        crc = binascii.crc_hqx(binary, 0xFFFF)
        if (init.get('device_type') != 82 or init.get('softdevice_req') != [0xB6]
                or init.get('firmware_crc16') != crc):
            raise ValueError('Wrong target/SoftDevice or mismatched firmware CRC')
        expected_dat = struct.pack('<HHIHHH', 82, init['device_revision'],
                                   init['application_version'], 1, 0xB6, crc)
        if package.read(dat_name) != expected_dat:
            raise ValueError('DFU initialization packet does not match manifest/image')
    return {
        'application_start': hex(APP_START), 'application_end': hex(APP_START + len(binary)),
        'uf2_sha256': hashlib.sha256(blob).hexdigest(),
        'dfu_sha256': hashlib.sha256(zip_path.read_bytes()).hexdigest(),
    }
