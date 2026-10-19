#!/usr/bin/env python3
"""Build native firmware for either RTOS, without touching USB."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
from bootstrap import ROOT, LOCK

RTOS_TYPES = ('zephyr', 'freertos')

def build(rtos, version='dev'):
    if rtos not in RTOS_TYPES or not re.fullmatch(r'[A-Za-z0-9.+_-]{1,64}', version):
        raise ValueError('Invalid RTOS or version')
    deps, output = ROOT / '.deps', ROOT / 'build' / rtos
    cfg = LOCK['toolchains'][platform.system() + '-' + platform.machine()]
    env = dict(os.environ)
    env['PATH'] = str(Path(sys.executable).parent) + os.pathsep + env.get('PATH', '')
    cmd = ['cmake', '-G', 'Ninja', '-S', str(ROOT / 'firmware' / rtos), '-B', str(output),
           '-DRADIOLIB_DIR=' + str(deps / 'RadioLib'), '-DM1_VERSION=' + version,
           '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    if rtos == 'zephyr':
        env['ZEPHYR_BASE'] = str(deps / 'zephyr')
        env['ZEPHYR_SDK_INSTALL_DIR'] = str(ROOT / '.tools' / ('zephyr-sdk-' + LOCK['zephyr_sdk_version']))
        cmd += ['-DBOARD=thinknode_m1/nrf52840']
    else:
        cmd += ['-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/arm-gcc.cmake'),
                '-DARM_TOOLCHAIN_PREFIX=' + str(ROOT / '.tools' / cfg['arm_name'] / 'bin/arm-none-eabi-'),
                '-DNRF5_SDK_DIR=' + str(deps / LOCK['nrf5_sdk']['path']),
                '-DFREERTOS_DIR=' + str(deps / 'FreeRTOS-Kernel')]
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'build.log').open('w') as log:
        for argv in (cmd, ['cmake', '--build', str(output), '--parallel', str(min(os.cpu_count() or 2, 8))]):
            result = subprocess.run(argv, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                print((output / 'build.log').read_text()[-12000:], file=sys.stderr)
                raise RuntimeError(f'{rtos} build failed; see {output / "build.log"}')
    # Reject accidental wiring runtime defines/includes in compiler input, not just filenames.
    commands = (output / 'compile_commands.json').read_text()
    if re.search(r'(?:-DARDUINO(?:=|\s)|framework-arduino|Adafruit_TinyUSB|Adafruit_GFX|GxEPD2)', commands):
        raise RuntimeError('Unexpected wiring-runtime dependency in native build')
    print(f'Built {rtos}: {output}')
    return output

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtos', choices=(*RTOS_TYPES, 'all'), required=True)
    parser.add_argument('--version', default='dev')
    args = parser.parse_args()
    for target in RTOS_TYPES if args.rtos == 'all' else [args.rtos]:
        build(target, args.version)
