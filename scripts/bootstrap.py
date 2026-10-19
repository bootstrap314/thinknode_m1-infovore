#!/usr/bin/env python3
"""Install pinned native dependencies inside this project. Never accesses a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'dependencies.lock.json').read_text())
MODULES = ('cmsis', 'cmsis_6', 'hal_nordic', 'mbedtls', 'tf-psa-crypto')

def run(*args, cwd=ROOT, **kwargs):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True, **kwargs)

def download(url, digest):
    target = ROOT / '.deps' / 'downloads' / url.rsplit('/', 1)[1]
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        partial = target.with_suffix('.partial')
        run('curl', '--fail', '--location', '--retry', '3', '--silent', '--show-error', url, '-o', partial)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != digest:
            partial.unlink()
            raise RuntimeError('Checksum mismatch: ' + target.name)
        partial.replace(target)
    return target

def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as package:
            for member in package.namelist():
                if not (destination / member).resolve().is_relative_to(destination.resolve()):
                    raise RuntimeError('Unsafe archive path')
            package.extractall(destination)
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter='data')

def source(name):
    dep = LOCK[name]
    path = ROOT / '.deps' / dep['path']
    if not (path / '.git').exists():
        path.mkdir(parents=True, exist_ok=True)
        run('git', 'init', path)
        run('git', 'remote', 'add', 'origin', dep['url'], cwd=path)
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=path, text=True, capture_output=True)
    if head.returncode or head.stdout.strip() != dep['revision']:
        if subprocess.check_output(['git', 'status', '--porcelain'], cwd=path).strip():
            raise RuntimeError(f'{path} has edits; refusing to replace them')
        run('git', 'fetch', '--depth', '1', 'origin', dep['revision'], cwd=path)
        run('git', 'checkout', '--detach', dep['revision'], cwd=path)
    return path

def bootstrap(rtos):
    host = platform.system() + '-' + platform.machine()
    if host not in LOCK['toolchains']:
        raise RuntimeError('Supported hosts: Apple Silicon macOS and x86_64 Linux (or Linux CI)')
    cfg = LOCK['toolchains'][host]
    source('radiolib')
    tools = ROOT / '.tools'
    if rtos in ('freertos', 'all'):
        source('freertos')
        sdk = LOCK['nrf5_sdk']
        if not (ROOT / '.deps' / sdk['path']).is_dir():
            extract(download(sdk['url'], sdk['sha256']), ROOT / '.deps')
        gcc = tools / cfg['arm_name']
        if not (gcc / 'bin' / 'arm-none-eabi-gcc').exists():
            base = 'https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/'
            extract(download(base + cfg['arm_name'] + '.tar.xz', cfg['arm_sha256']), tools)
    if rtos in ('zephyr', 'all'):
        zephyr = source('zephyr')
        if not (ROOT / '.deps' / '.west').exists():
            run(sys.executable, '-m', 'west', 'init', '-l', zephyr)
        run(sys.executable, '-m', 'west', 'update', *MODULES, cwd=ROOT / '.deps')
        run(sys.executable, '-m', 'pip', 'install', '-r', zephyr / 'scripts/requirements-base.txt')
        version = LOCK['zephyr_sdk_version']
        sdk = tools / ('zephyr-sdk-' + version)
        base = f'https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v{version}/'
        if not (sdk / 'sdk_version').exists():
            extract(download(base + f'zephyr-sdk-{version}_{cfg["zephyr_host"]}_minimal.tar.xz',
                             cfg['zephyr_minimal_sha256']), tools)
        if not (sdk / 'gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc').exists():
            extract(download(base + f'toolchain_gnu_{cfg["zephyr_host"]}_arm-zephyr-eabi.tar.xz',
                             cfg['zephyr_arm_sha256']), sdk / 'gnu')
    print('Native dependencies ready in ' + str(ROOT))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtos', choices=('zephyr', 'freertos', 'all'), default='all')
    args = parser.parse_args()
    bootstrap(args.rtos)
