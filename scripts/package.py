#!/usr/bin/env python3
"""Produce deterministic application-only UF2 and legacy serial-DFU packages."""
import argparse
import binascii
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile
from intelhex import IntelHex
from elftools.elf.elffile import ELFFile
from firmware_artifacts import APP_START, APP_END, FAMILY, validate
from bootstrap import ROOT, LOCK


def check_elf(path, binary):
    with path.open('rb') as source:
        elf = ELFFile(source)
        if elf['e_machine'] != 'EM_ARM':
            raise ValueError('Firmware is not an ARM executable')
        for segment in elf.iter_segments():
            if segment['p_type'] != 'PT_LOAD' or not segment['p_filesz']:
                continue
            start = segment['p_paddr']
            if not APP_START <= start < start + segment['p_filesz'] <= APP_END:
                raise ValueError('ELF contains data outside application flash')
    stack, reset = struct.unpack_from('<II', binary)
    if not 0x20000000 < stack <= 0x20040000 or stack % 8 or not reset & 1 or not APP_START <= (reset & ~1) < APP_START + len(binary):
        raise ValueError('Invalid application vector table')


def package(rtos, version='dev', commit='local'):
    if rtos not in ('zephyr', 'freertos'):
        raise ValueError('Unknown RTOS')
    build = ROOT / 'build' / rtos
    source = build / 'zephyr/zephyr' if rtos == 'zephyr' else build / 'firmware'
    elf, hex_path = source.with_suffix('.elf'), source.with_suffix('.hex')
    ihex = IntelHex(str(hex_path))
    if ihex.minaddr() != APP_START or ihex.maxaddr() >= APP_END:
        raise ValueError('HEX writes protected flash')
    ihex.padding = 0xff
    binary = ihex.tobinstr(start=APP_START, end=ihex.maxaddr())
    check_elf(elf, binary)
    out = ROOT / 'dist' / rtos
    out.mkdir(parents=True, exist_ok=True)
    # Fixed filenames let users select RTOS without interpreting a build timestamp.
    prefix = 'thinknode-m1-' + rtos
    allowed = {prefix + ext for ext in ('.uf2', '-dfu.zip', '-support.zip', '.elf', '.hex', '-manifest.json')}
    allowed |= {'SHA256SUMS.txt', 'verification-' + rtos + '.json'}
    if any(p.name not in allowed or not p.is_file() for p in out.iterdir()):
        raise ValueError('Unexpected files in dist/' + rtos + '; use a clean output folder')
    uf2, dfu = out / (prefix + '.uf2'), out / (prefix + '-dfu.zip')
    padded = binary + b'\xff' * (-len(binary) % 256)
    blocks = []
    for i in range(len(padded) // 256):
        header = struct.pack('<8I', 0x0A324655, 0x9E5D5157, 0x2000, APP_START+i*256, 256, i, len(padded)//256, FAMILY)
        blocks.append(header + padded[i*256:(i+1)*256] + bytes(220) + struct.pack('<I', 0x0AB16F30))
    uf2.write_bytes(b''.join(blocks))
    init = {'device_type': 82, 'device_revision': 65535, 'application_version': 4294967295,
            'softdevice_req': [0xB6], 'firmware_crc16': binascii.crc_hqx(binary, 0xffff)}
    manifest = {'manifest': {'application': {'bin_file': 'application.bin', 'dat_file': 'application.dat', 'init_packet_data': init}, 'dfu_version': 0.5}}
    members = {'application.bin': binary, 'application.dat': struct.pack('<HHIHHH', 82, 65535, 4294967295, 1, 0xB6, init['firmware_crc16']),
               'manifest.json': (json.dumps(manifest, sort_keys=True) + '\n').encode()}
    with zipfile.ZipFile(dfu, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name, content in sorted(members.items()):
            archive.writestr(zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)), content)
    # Ship the host client, protocol guide and retained notices with either image.
    support = [ROOT / 'README.md', ROOT / 'scripts/m1.py'] + sorted((ROOT / 'docs').glob('*.md'))
    support += sorted((ROOT / 'licenses').glob('*'))
    with zipfile.ZipFile(out / (prefix + '-support.zip'), 'w') as archive:
        for path in support:
            archive.writestr(zipfile.ZipInfo(path.relative_to(ROOT).as_posix(), (1980, 1, 1, 0, 0, 0)), path.read_bytes())
    info = validate(uf2, dfu)
    shutil.copyfile(elf, out / (prefix + '.elf'))
    shutil.copyfile(hex_path, out / (prefix + '.hex'))
    info.update(rtos=rtos, version=version, source_commit=commit, credentials_embedded=False,
                hardware_validated=False, bootloader='Elecrow/Adafruit, S140 6.1.1 (FWID 0xB6)',
                dependencies={k: v['version'] for k, v in LOCK.items() if isinstance(v, dict) and 'version' in v},
                application_limit=hex(APP_END))
    (out / (prefix + '-manifest.json')).write_text(json.dumps(info, indent=2) + '\n')
    return info


def checksums(folder):
    folder = Path(folder)
    lines = [hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(folder).as_posix()
             for p in sorted(folder.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS.txt']
    (folder / 'SHA256SUMS.txt').write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rtos', choices=('zephyr', 'freertos'), required=True)
    p.add_argument('--version', default='dev')
    p.add_argument('--commit', default='local')
    args = p.parse_args()
    package(args.rtos, args.version, args.commit)
    checksums(ROOT / 'dist' / args.rtos)
