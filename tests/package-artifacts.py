#!/usr/bin/env python3
"""Exercise package-manager artifact transport using a temporary HTTP registry.

FORGE_PM_BINARY defaults to build/forge-cli. FORGE_PM_COMMAND can instead be a
JSON argv array with {cwd}, {registry}, {path} substitutions for a container
runner. The test HTTP server binds loopback; container runners use host network.
"""
import hashlib
import http.server
import io
import json
import os
import shutil
from pathlib import Path
import subprocess
import tarfile
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ.get('FORGE_PM_BINARY', str(ROOT / 'build/forge-cli'))


def archive(entries):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w:gz') as output:
        for name, kind, body in entries:
            item = tarfile.TarInfo(name)
            item.mode = 0o644
            if kind == 'symlink':
                item.type = tarfile.SYMTYPE
                item.linkname = body
                output.addfile(item)
            elif kind == 'hardlink':
                item.type = tarfile.LNKTYPE
                item.linkname = body
                output.addfile(item)
            elif kind == 'device':
                item.type = tarfile.CHRTYPE
                output.addfile(item)
            else:
                item.size = len(body)
                output.addfile(item, io.BytesIO(body))
    return data.getvalue()


class ArtifactTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = b''
        cls.release = {}
        cls.requests = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                cls.requests.append(self.path)
                if self.path == '/artifact.tar.gz':
                    data = cls.data
                elif self.path == '/api/packages/artifact-module/versions/0.1.0':
                    data = json.dumps(cls.release).encode()
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.registry = 'http://127.0.0.1:' + str(cls.server.server_port)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='forge-artifact-test-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.shims = self.directory / 'bin'
        self.shims.mkdir()
        (self.directory / 'forge.json').write_text(json.dumps({
            'name': 'artifact-app', 'entry': 'main.fg',
            'dependencies': {'artifact-module': '0.1.0'}}))
        (self.directory / 'forge.lock').write_text('previous-lock\n')
        self.commit = 'a' * 40
        self.log = self.directory / 'git.log'
        shim = self.shims / 'git'
        shim.write_text(f'''#!/bin/sh
printf '%s\\n' "$*" >> '{self.log}'
case "$*" in
 *clone*) for last do :; done; mkdir -p "$last/.git"; printf 'fn demo(): int {{ return 1; }}\\n' > "$last/demo.fg" ;;
 *rev-parse*) printf '{self.commit}\\n' ;;
esac
''')
        shim.chmod(0o755)
        type(self).requests.clear()

    def prepare(self, entries, checksum=None, size=None):
        cls = type(self)
        cls.data = archive(entries)
        release = json.loads((ROOT / 'backend/seed.json').read_text())[0]
        release.update(name='artifact-module', version='0.1.0', module='demo.fg',
                       git_commit=self.commit, dependencies={})
        release.pop('native', None)
        release.pop('javascript', None)
        release['artifact'] = {
            'sha256': checksum or hashlib.sha256(cls.data).hexdigest(),
            'size': size if size is not None else len(cls.data),
            'url': cls.registry + '/artifact.tar.gz'}
        cls.release = release

    def run_cli(self):
        environment = dict(os.environ)
        environment['FORGE_REGISTRY'] = self.registry
        environment['PATH'] = str(self.shims) + ':' + environment.get('PATH', '')
        template = json.loads(os.environ.get('FORGE_PM_COMMAND', '[]')) or [BINARY]
        replacements = dict(cwd=str(self.directory), registry=self.registry, path=environment['PATH'])
        command = [part.format(**replacements) for part in template] + ['install']
        return subprocess.run(command, cwd=self.directory, env=environment,
                              capture_output=True, text=True, timeout=30)

    def require_failed_cleanly(self):
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.directory / 'forge.lock').read_text(), 'previous-lock\n')
        self.assertFalse(self.log.exists(), 'Artifact errors must never fall back to Git')
        self.assertEqual(list((self.directory / '.forge').glob('staging-*')), [])

    def test_success_uses_verified_marker_and_reuses_cache(self):
        self.prepare([('repo-commit/demo.fg', 'file', b'fn demo(): int { return 1; }\n')])
        first = self.run_cli()
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        directory = self.directory / '.forge/packages/artifact-module' / self.commit
        self.assertTrue((directory / 'demo.fg').is_file())
        marker = json.loads((directory / '.forge-artifact').read_text())
        self.assertEqual(marker['sha256'], type(self).release['artifact']['sha256'])
        self.assertEqual(marker['git_commit'], self.commit)
        self.assertFalse((directory / '.git').exists())
        self.assertFalse(self.log.exists())
        self.assertEqual(type(self).requests.count('/artifact.tar.gz'), 1)
        again = self.run_cli()
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)
        self.assertEqual(type(self).requests.count('/artifact.tar.gz'), 1)

    def test_pax_utf8_paths_are_preserved_in_c_locale(self):
        self.prepare([('repo/demo.fg', 'file', b'fn demo(): int { return 1; }\n'),
                      ('repo/자료/문서 — 안내.md', 'file', '한글 문서'.encode())])
        previous = os.environ.get('LC_ALL')
        os.environ['LC_ALL'] = 'C'
        try:
            result = self.run_cli()
        finally:
            if previous is None:
                os.environ.pop('LC_ALL', None)
            else:
                os.environ['LC_ALL'] = previous
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        directory = self.directory / '.forge/packages/artifact-module' / self.commit
        self.assertEqual((directory / '자료/문서 — 안내.md').read_bytes(), '한글 문서'.encode())

    def test_checksum_failure_preserves_lock(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')], checksum='0' * 64)
        self.require_failed_cleanly()

    def test_size_failure_preserves_lock(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')], size=1)
        self.require_failed_cleanly()

    def test_unsafe_archive_paths_and_types_are_rejected(self):
        for malicious in [
            ('repo/../escape', 'file', b'x'),
            ('/absolute', 'file', b'x'),
            ('repo/.git/config', 'file', b'x'),
            ('repo/.forge-artifact', 'file', b'x'),
            ('repo/.forge-source.tar.gz', 'file', b'x'),
            ('repo/build/payload.cmake', 'file', b'x'),
            ('repo/node_modules/payload.js', 'file', b'x'),
            ('repo/link', 'symlink', '../../escape'),
            ('repo/link', 'hardlink', 'repo/demo.fg'),
            ('repo/device', 'device', b''),
            ('different-root/extra', 'file', b'x'),
            ('repo/demo.fg', 'file', b'duplicate'),
        ]:
            with self.subTest(malicious=malicious):
                self.prepare([('repo/demo.fg', 'file', b'ok'), malicious])
                self.require_failed_cleanly()
                self.assertFalse((self.directory / 'escape').exists())

    def test_missing_declared_module_is_rejected(self):
        self.prepare([('repo/unrelated.fg', 'file', b'ok')])
        self.require_failed_cleanly()

    def test_artifact_without_gzip_is_rejected(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')])
        type(self).data = b'not an archive'
        type(self).release['artifact'].update(
            sha256=hashlib.sha256(type(self).data).hexdigest(), size=len(type(self).data))
        self.require_failed_cleanly()

    def test_entry_count_is_bounded(self):
        entries = [('repo/demo.fg', 'file', b'ok')]
        entries.extend(('repo/files/file-' + str(i), 'file', b'x') for i in range(10000))
        self.prepare(entries)
        self.require_failed_cleanly()

    def test_declared_expanded_size_is_bounded(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')])
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode='w:gz') as data:
            item = tarfile.TarInfo('repo/demo.fg')
            item.size = 256 * 1024 * 1024 + 1
            data.addfile(item)
        type(self).data = output.getvalue()
        type(self).release['artifact'].update(
            sha256=hashlib.sha256(type(self).data).hexdigest(), size=len(type(self).data))
        self.require_failed_cleanly()

    def prepare_git_transport(self):
        fixture = self.directory / 'fixture-repository'
        fixture.mkdir()
        (fixture / 'demo.fg').write_text('fn demo(): int { return 1; }\n')
        executable = shutil.which('git')
        env = {**os.environ, 'GIT_MASTER': '1'}
        for args in [('init', '-q'), ('config', 'user.name', 'Fixture'), ('config', 'user.email', 'fixture@example.invalid'), ('add', '--all'), ('commit', '-qm', 'Pinned fixture')]:
            subprocess.run([executable, *args], cwd=fixture, env=env, check=True, capture_output=True)
        self.commit = subprocess.check_output([executable, 'rev-parse', 'HEAD'], cwd=fixture, env=env, text=True).strip()
        type(self).release['git_commit'] = self.commit
        canonical = type(self).release['repository_url']
        shim = self.shims / 'git'
        # Only transport is substituted: checkout and security validation use real Git.
        shim.write_text(f'''#!/bin/sh
printf '%s\\n' "$*" >> '{self.log}'
case "$*" in
 *clone*) for last do :; done
  GIT_MASTER=1 '{executable}' clone --quiet --no-hardlinks -- '{fixture}' "$last" || exit $?
  GIT_MASTER=1 '{executable}' -C "$last" remote set-url origin '{canonical}'
  ;;
 *) exec '{executable}' "$@" ;;
esac
''')
        shim.chmod(0o755)

    def test_cached_source_corruption_cannot_be_hidden_by_marker(self):
        self.prepare([('repo/demo.fg', 'file', b'fn demo(): int { return 1; }\n')])
        self.assertEqual(self.run_cli().returncode, 0)
        directory = self.directory / '.forge/packages/artifact-module' / self.commit
        (directory / 'demo.fg').write_text('fn demo(): int { return 999; }\n')
        # Rewriting marker metadata must not authorize corrupted extracted source.
        (directory / '.forge-artifact').write_text(json.dumps({'sha256': type(self).release['artifact']['sha256'], 'git_commit': self.commit}))
        lock = (self.directory / 'forge.lock').read_bytes()
        self.assertNotEqual(self.run_cli().returncode, 0)
        self.assertEqual((self.directory / 'forge.lock').read_bytes(), lock)
        self.assertFalse(self.log.exists())

    def test_cached_archive_corruption_and_extra_source_are_rejected(self):
        self.prepare([('repo/demo.fg', 'file', b'fn demo(): int { return 1; }\n')])
        self.assertEqual(self.run_cli().returncode, 0)
        directory = self.directory / '.forge/packages/artifact-module' / self.commit
        source_archive = directory / '.forge-source.tar.gz'
        original = source_archive.read_bytes()
        source_archive.write_bytes(b'corrupt')
        self.assertNotEqual(self.run_cli().returncode, 0)
        source_archive.write_bytes(original)
        (directory / 'extra.fg').write_text('fn injected(): int { return 1; }\n')
        self.assertNotEqual(self.run_cli().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_cached_source_link_is_rejected(self):
        self.prepare([('repo/demo.fg', 'file', b'fn demo(): int { return 1; }\n')])
        self.assertEqual(self.run_cli().returncode, 0)
        directory = self.directory / '.forge/packages/artifact-module' / self.commit
        (directory / 'demo.fg').unlink()
        (directory / 'demo.fg').symlink_to(self.directory / 'forge.json')
        self.assertNotEqual(self.run_cli().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_null_artifact_uses_existing_git_transport(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')])
        type(self).release['artifact'] = None
        self.prepare_git_transport()
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('clone', self.log.read_text())
        self.assertNotIn('/artifact.tar.gz', type(self).requests)

    def test_legacy_release_uses_existing_git_transport(self):
        self.prepare([('repo/demo.fg', 'file', b'ok')])
        type(self).release.pop('artifact')
        self.prepare_git_transport()
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('clone', self.log.read_text())
        self.assertIn('checkout', self.log.read_text())
        self.assertIn('rev-parse', self.log.read_text())
        self.assertNotIn('/artifact.tar.gz', type(self).requests)


if __name__ == '__main__':
    unittest.main()
