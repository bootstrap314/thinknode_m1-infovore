import json
import hashlib
import shutil
import test_firmware_artifacts as fixtures
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release

class Releases(unittest.TestCase):
    def test_tag_syntax(self):
        for tag in ('v1.0.0', 'v2.1.3-rc.1'):
            self.assertEqual(release.validate_tag(tag), tag)
        for tag in ('main', 'v01.0.0', 'v1.0', '../v1.0.0', 'v1.0.0;touch x', '$(id)', 'v1.0.0\n'):
            with self.assertRaises(ValueError): release.validate_tag(tag)
    def test_requires_both_verified_firmwares(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError): release.verify_release(directory, 'v1.0.0', 'a'*40)
    def test_resolves_remote_annotated_and_lightweight_tags(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); remote=root/'remote'; local=root/'local'
            def git(*args, cwd=None):
                return subprocess.check_output(['git', *args], cwd=cwd, stderr=subprocess.DEVNULL, text=True).strip()
            git('init', str(remote));git('config', 'user.name', 'Test', cwd=remote);git('config','user.email','test@example.invalid',cwd=remote);git('config','commit.gpgsign','false',cwd=remote);git('config','tag.gpgsign','false',cwd=remote)
            git('commit','--allow-empty','-m','first',cwd=remote)
            sha=git('rev-parse','HEAD',cwd=remote)
            git('tag','v1.0.0',cwd=remote);git('tag','-a','v1.0.1','-m','annotated',cwd=remote)
            git('clone',str(remote),str(local))
            for tag in ('v1.0.0','v1.0.1'): self.assertEqual(release.resolve_tag(tag,local),sha)
            with self.assertRaises(subprocess.CalledProcessError): release.resolve_tag('v9.9.9',local)

class ReleaseBundles(unittest.TestCase):
    def setUp(self):
        fixture = fixtures.Artifacts()
        fixture.setUp()
        self.addCleanup(fixture.temp.cleanup)
        self.root = fixture.root / 'release'
        self.root.mkdir()
        self.tag, self.commit = 'v1.0.0', 'a' * 40
        for rtos in ('zephyr', 'freertos'):
            prefix = 'thinknode-m1-' + rtos
            for original, suffix in ((fixture.uf2, '.uf2'), (fixture.zip, '-dfu.zip')):
                shutil.copyfile(original, self.root / (prefix + suffix))
            for suffix in ('.elf', '.hex', '-support.zip'):
                (self.root / (prefix + suffix)).write_bytes(b'test fixture')
            info = release.validate(self.root / (prefix + '.uf2'), self.root / (prefix + '-dfu.zip'))
            info.update(rtos=rtos, version=self.tag, source_commit=self.commit, credentials_embedded=False)
            (self.root / (prefix + '-manifest.json')).write_text(json.dumps(info))
            report = {'passed': True, 'firmware_built': True, 'source_sha256': 'b'*64, 'firmware': {rtos: info}}
            (self.root / ('verification-' + rtos + '.json')).write_text(json.dumps(report))
            self.checksums(rtos)

    def checksums(self, rtos):
        names = ['thinknode-m1-' + rtos + ext for ext in ('.uf2', '-dfu.zip', '-support.zip', '.elf', '.hex', '-manifest.json')]
        names.append('verification-' + rtos + '.json')
        text = ''.join(hashlib.sha256((self.root / name).read_bytes()).hexdigest() + '  ' + name + '\n' for name in names)
        (self.root / ('SHA256SUMS-' + rtos + '.txt')).write_text(text)

    def verify(self):
        return release.verify_release(self.root, self.tag, self.commit)

    def test_complete_bundle_and_wrong_commit(self):
        self.assertTrue(self.verify())
        with self.assertRaisesRegex(ValueError, 'tag/commit'):
            release.verify_release(self.root, self.tag, 'c'*40)

    def test_tampered_support_archive(self):
        (self.root / 'thinknode-m1-zephyr-support.zip').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'checksum'): self.verify()

    def test_different_source_snapshots(self):
        path = self.root / 'verification-freertos.json'
        report = json.loads(path.read_text())
        report['source_sha256'] = 'c'*64
        path.write_text(json.dumps(report)); self.checksums('freertos')
        with self.assertRaisesRegex(ValueError, 'different source'): self.verify()

    def test_missing_second_firmware(self):
        (self.root / 'thinknode-m1-freertos.uf2').unlink()
        with self.assertRaises(FileNotFoundError): self.verify()

if __name__ == '__main__': unittest.main()

