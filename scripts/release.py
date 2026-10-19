#!/usr/bin/env python3
"""Resolve an existing pushed release tag and verify a complete two-RTOS release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from firmware_artifacts import validate

TAG = re.compile(r'v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?')

def validate_tag(tag):
    if not TAG.fullmatch(tag) or len(tag) > 64:
        raise ValueError('Use an existing version tag such as v1.0.0 or v1.0.0-rc.1')
    return tag

def resolve_tag(tag, cwd):
    validate_tag(tag)
    subprocess.run(['git', 'fetch', '--no-tags', 'origin', f'+refs/tags/{tag}:refs/tags/{tag}'], cwd=cwd, check=True)
    sha = subprocess.check_output(['git', 'rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}'], cwd=cwd, text=True).strip()
    if not re.fullmatch('[0-9a-f]{40}', sha): raise ValueError('Tag did not resolve to a commit')
    return sha

def verify_release(folder, tag, commit):
    folder = Path(folder)
    validate_tag(tag)
    sources = set()
    for rtos in ('zephyr', 'freertos'):
        prefix = 'thinknode-m1-' + rtos
        info = json.loads((folder / (prefix + '-manifest.json')).read_text())
        report = json.loads((folder / ('verification-' + rtos + '.json')).read_text())
        if info['rtos'] != rtos or info['version'] != tag or info['source_commit'] != commit or info['credentials_embedded']:
            raise ValueError('Release manifest does not match selected tag/commit')
        if not report['passed'] or not report['firmware_built'] or report['firmware'][rtos] != info:
            raise ValueError('Missing or mismatched firmware verification report')
        source = report.get('source_sha256', '')
        if not re.fullmatch('[0-9a-f]{64}', source): raise ValueError('Missing source fingerprint')
        sources.add(source)
        actual = validate(folder / (prefix + '.uf2'), folder / (prefix + '-dfu.zip'))
        for key in ('uf2_sha256', 'dfu_sha256'):
            if actual[key] != info[key]: raise ValueError('Release artifact hash mismatch')
        lines = (folder / ('SHA256SUMS-' + rtos + '.txt')).read_text().splitlines()
        expected = {prefix + ext for ext in ('.uf2', '-dfu.zip', '-support.zip', '.elf', '.hex', '-manifest.json')}
        expected.add('verification-' + rtos + '.json')
        seen = set()
        for line in lines:
            digest, name = line.split('  ', 1)
            if Path(name).name != name or name in seen: raise ValueError('Unsafe or duplicate checksum filename')
            seen.add(name)
            if hashlib.sha256((folder / name).read_bytes()).hexdigest() != digest: raise ValueError('Release checksum mismatch')
        if expected != seen: raise ValueError('Unexpected or incomplete release checksum list')
    if len(sources) != 1: raise ValueError('RTOS builds used different source snapshots')
    return True

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('resolve', 'verify'))
    p.add_argument('--tag', required=True)
    p.add_argument('--commit')
    p.add_argument('--folder', default='.')
    args = p.parse_args()
    if args.command == 'resolve': print(resolve_tag(args.tag, Path.cwd()))
    else: verify_release(args.folder, args.tag, args.commit)
