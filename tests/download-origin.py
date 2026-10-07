#!/usr/bin/env python3
"""Verify installer download routing without fetching or installing a release."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

INSTALL = Path(__file__).resolve().parents[1] / 'scripts/install.sh'


class DownloadOriginTest(unittest.TestCase):
    def check_origin(self, origin, expected):
        with tempfile.TemporaryDirectory(prefix='forge-download-origin-') as directory:
            root = Path(directory)
            tools = root / 'tools'
            tools.mkdir()
            mock = tools / 'curl'
            mock.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$CURL_CAPTURE"\nexit 22\n')
            mock.chmod(0o755)
            capture = root / 'curl-arguments.txt'
            profile = root / 'profile'
            profile.mkdir()
            env = {**os.environ, 'PATH': str(tools) + ':' + os.environ['PATH'],
                   'FORGE_HOME': str(root / 'sdk'), 'FORGE_PROFILE_ROOT': str(profile),
                   'CURL_CAPTURE': str(capture)}
            env.pop('FORGE_DOWNLOAD_BASE', None)
            if origin is not None:
                env['FORGE_DOWNLOAD_BASE'] = origin
            result = subprocess.run(['bash', str(INSTALL), '--no-modify-path'],
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 22, result.stderr)
            urls = [arg for arg in capture.read_text().splitlines() if arg.startswith('http')]
            self.assertEqual(urls, [expected + '.sha256'])
            self.assertFalse(list((root / 'sdk').glob('.install.*')))

    def test_default_canonical_github(self):
        self.check_origin(None, 'https://github.com/forge-language/forge/releases/download/v0.3.0-preview.5/forge-0.3.0-preview.5-linux-x86_64.tar.gz')

    def test_explicit_canonical_github_trailing_slash(self):
        self.check_origin('https://github.com/forge-language/forge/', 'https://github.com/forge-language/forge/releases/download/v0.3.0-preview.5/forge-0.3.0-preview.5-linux-x86_64.tar.gz')

    def test_custom_loopback_hosting(self):
        self.check_origin('http://127.0.0.1:18299/', 'http://127.0.0.1:18299/releases/0.3.0-preview.5/forge-0.3.0-preview.5-linux-x86_64.tar.gz')


if __name__ == '__main__':
    unittest.main()
