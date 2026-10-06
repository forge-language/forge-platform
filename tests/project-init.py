#!/usr/bin/env python3
"""Exercise project creation in isolated directories using the built manager."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MANAGER = Path(os.environ.get('FORGE_PM', ROOT / 'build/forge-cli')).resolve()


class ProjectInitTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='forge-project-init-')
        self.parent = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def init(self, *args):
        return subprocess.run([str(MANAGER), 'init', *args], cwd=self.parent,
                              capture_output=True, text=True, timeout=15)

    def check_project(self, directory, name):
        self.assertEqual(json.loads((directory / 'forge.json').read_text()),
                         {'name': name, 'entry': 'main.fg', 'dependencies': {}})
        self.assertIn('Hello, Forge!', (directory / 'main.fg').read_text())

    def test_named_directory_leaves_parent_untouched(self):
        (self.parent / 'forge.json').write_text('existing parent project')
        (self.parent / 'main.fg').write_text('existing source')
        result = self.init('hello-app')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.check_project(self.parent / 'hello-app', 'hello-app')
        self.assertEqual((self.parent / 'forge.json').read_text(), 'existing parent project')
        self.assertEqual((self.parent / 'main.fg').read_text(), 'existing source')
        self.assertIn('cd hello-app && forge run', result.stdout)

    def test_default_directory(self):
        result = self.init()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.check_project(self.parent / 'forge-app', 'forge-app')
        self.assertFalse((self.parent / 'forge.json').exists())

    def test_existing_directory_and_file_are_preserved(self):
        for kind in ('empty', 'populated', 'file', 'symlink'):
            with self.subTest(kind=kind):
                target = self.parent / ('existing-' + kind)
                if kind == 'file':
                    target.write_text('keep')
                elif kind == 'symlink':
                    target.symlink_to(self.parent / 'missing')
                else:
                    target.mkdir()
                    if kind == 'populated':
                        (target / 'main.fg').write_text('keep')
                self.assertNotEqual(self.init(target.name).returncode, 0)
                if kind == 'file':
                    self.assertEqual(target.read_text(), 'keep')
                elif kind == 'symlink':
                    self.assertTrue(target.is_symlink())
                    self.assertFalse((self.parent / 'missing').exists())
                else:
                    self.assertFalse((target / 'forge.json').exists())
                    if kind == 'populated':
                        self.assertEqual((target / 'main.fg').read_text(), 'keep')

    def test_invalid_names_do_not_write_files(self):
        for name in ('../escape', '/tmp/escape', 'nested/app', 'Bad name', '--bad', 'ab'):
            with self.subTest(name=name):
                self.assertNotEqual(self.init(name).returncode, 0)
                self.assertEqual(list(self.parent.iterdir()), [])

    def test_explicit_current_directory_preserves_source(self):
        (self.parent / 'main.fg').write_text('native main { return 42; }\n')
        result = self.init('.')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads((self.parent / 'forge.json').read_text())['name'], 'forge-app')
        self.assertEqual((self.parent / 'main.fg').read_text(), 'native main { return 42; }\n')

    def test_current_directory_existing_manifest_is_preserved(self):
        (self.parent / 'forge.json').write_text('keep')
        self.assertNotEqual(self.init('.').returncode, 0)
        self.assertEqual((self.parent / 'forge.json').read_text(), 'keep')
        self.assertFalse((self.parent / 'main.fg').exists())

    def test_extra_arguments_do_not_create_project(self):
        self.assertNotEqual(self.init('hello-app', 'unexpected').returncode, 0)
        self.assertEqual(list(self.parent.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
