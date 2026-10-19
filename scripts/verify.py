#!/usr/bin/env python3
"""Host tests and optional native builds/packages. Never accesses USB."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from bootstrap import ROOT

REPORT = ROOT / 'build' / 'verification'
SOURCE_DIRS = ('firmware', 'include', 'cmake', 'scripts', 'tests', 'chirpstack', '.github', 'docs', 'licenses')

def source_digest():
    digest = hashlib.sha256()
    files = [ROOT / 'dependencies.lock.json', ROOT / 'requirements-dev.txt', ROOT / 'README.md']
    for folder in SOURCE_DIRS:
        files.extend(p for p in (ROOT / folder).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix != '.pyc' and p.name != 'secrets.h')
    for path in sorted(files):
        digest.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()

def run(argv, name):
    print('Checking ' + name, flush=True)
    subprocess.run([str(x) for x in argv], cwd=ROOT, check=True)

def host_tests():
    REPORT.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get('CXX', shutil.which('clang++') or 'c++'))
    tests = ('phone_commands_test', 'power_policy_test', 'power_simulation_test', 'display_diff_test', 'native_logic_test', 'native_display_test', 'native_output_test', 'button_policy_test', 'screen_ui_test')
    for name in tests:
        binary = REPORT / name
        sources = [ROOT / 'tests' / (name + '.cpp')]
        if name == 'native_display_test': sources += [ROOT / 'firmware/common/display.cpp']
        if name == 'native_output_test': sources += [ROOT / 'firmware/common/output.cpp']
        if name == 'screen_ui_test': sources += [ROOT / 'firmware/common/screen_ui.cpp']
        run(compiler + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-I', ROOT / 'include', '-I', ROOT / 'firmware/common',
                       *sources, '-o', binary], name + ' compile')
        run([binary], name)
    run([os.environ.get('NODE', 'node'), ROOT / 'tests/decoder_test.js'], 'ChirpStack decoder')
    run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py', '-v'], 'build/release rejection tests')
    return list(tests) + ['decoder_test', 'python_tests']

def verify(build=False, rtos='all', version='dev', commit='local'):
    if rtos not in ('all', 'zephyr', 'freertos'): raise ValueError('Invalid RTOS')
    REPORT.mkdir(parents=True, exist_ok=True)
    report_path = REPORT / ('report-' + rtos + '.json')
    report_path.unlink(missing_ok=True)
    before = source_digest()
    report = {'passed': False, 'source_sha256': before, 'firmware_built': build, 'hardware_validated': False,
              'checks': host_tests()}
    if build:
        from build import build as build_firmware
        from package import package, checksums
        report['firmware'] = {}
        for target in ('zephyr', 'freertos') if rtos == 'all' else [rtos]:
            build_firmware(target, version)
            report['firmware'][target] = package(target, version, commit)
    if source_digest() != before: raise RuntimeError('Source changed during verification; run again')
    report['passed'] = True
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    if build:
        for target in report['firmware']:
            shutil.copyfile(report_path, ROOT / 'dist' / target / ('verification-' + target + '.json'))
            checksums(ROOT / 'dist' / target)
    print('PASS: ' + ('host tests and native application-only images' if build else 'host tests'))
    return report

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', action='store_true')
    p.add_argument('--rtos', choices=('zephyr', 'freertos', 'all'), default='all')
    p.add_argument('--version', default='dev')
    p.add_argument('--commit', default='local')
    args = p.parse_args()
    try: verify(args.build, args.rtos, args.version, args.commit)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print('VERIFICATION FAILED: ' + str(error), file=sys.stderr);sys.exit(1)
