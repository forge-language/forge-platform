#!/usr/bin/env python3
"""Exercise pinned-checkout integrity and execution trust with real local Git fixtures."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest

spec = importlib.util.spec_from_file_location('build_cache', Path(__file__).with_name('build-cache.py'))
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class PackageSecurity(fixtures.BuildCacheTest):
    def native(self, trusted=False):
        module = self.add_module('native-module', {'native': {'library': 'native', 'cmake_target': 'native', 'pkg_config': []}})
        (module / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(Fixture C)\nadd_library(native STATIC native.c)\n')
        (module / 'native.c').write_text('int native(void) { return 1; }\n')
        return self.pin_module(module, trust_native=trusted)

    def git(self, module, *args):
        return subprocess.run(['git', '-c', 'core.hooksPath=/dev/null', *args], cwd=module,
                              env={**os.environ, 'GIT_MASTER': '1'}, text=True, capture_output=True, check=True)

    def rejected(self):
        result = self.run_manager('build')
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.calls('cmake'), 0, 'Unverified/untrusted source reached CMake')
        return result

    def test_security_native_requires_exact_commit_trust(self):
        module = self.native()
        self.assertIn('forge trust native-module', self.rejected().stdout)
        self.assert_success(self.run_manager('trust', 'native-module'))
        self.assert_success(self.run_manager('build'))
        (module / 'native.c').write_text('int native(void) { return 2; }\n')
        module = self.pin_module(module)
        self.assertIn('forge trust native-module', self.run_manager('build').stdout)
        self.assert_success(self.run_manager('trust', 'native-module'))
        self.assert_success(self.run_manager('build'))

    def test_security_modified_checkout_rejected_by_build_and_install(self):
        module = self.native(trusted=True)
        (module / 'native.c').write_text('int native(void) { return 9; }\n')
        self.rejected()
        lock = (self.project / 'forge.lock').read_bytes()
        catalog = list(json.loads(lock)['packages'].values())
        (self.toolchain / 'registry-seed.json').write_text(json.dumps(catalog))
        self.assertNotEqual(self.run_manager('install').returncode, 0)
        self.assertEqual((self.project / 'forge.lock').read_bytes(), lock)

    def test_security_cached_head_and_origin_checked(self):
        for mutation in ('head', 'origin'):
            with self.subTest(mutation=mutation):
                module = self.native(trusted=True)
                if mutation == 'head':
                    (module / 'native.c').write_text('int native(void) { return 3; }\n')
                    self.git(module, 'add', 'native.c')
                    self.git(module, 'commit', '-qm', 'unexpected commit')
                else:
                    self.git(module, 'remote', 'set-url', 'origin', 'https://github.com/attacker/module')
                self.rejected()
                # Reset fixture package directory for the next independent case.
                import shutil
                shutil.rmtree(self.project / '.forge')

    def test_security_source_symlink_and_gitdir_substitution_rejected(self):
        module = self.native(trusted=True)
        (module / 'module.fg').unlink()
        (module / 'module.fg').symlink_to(self.project / 'main.fg')
        self.rejected()
        (module / 'module.fg').unlink()
        self.git(module, 'checkout', '--', 'module.fg')
        metadata = module / '.git'
        metadata.rename(self.base / 'metadata')
        metadata.symlink_to(self.base / 'metadata', target_is_directory=True)
        self.rejected()

    def test_security_untracked_and_ignored_source_injection_rejected(self):
        module = self.native(trusted=True)
        (module / 'injected.fg').write_text('fn injected(): int { return 1; }\n')
        self.rejected()
        (module / 'injected.fg').unlink()
        # --others deliberately includes ignored files: ignore rules cannot hide source.
        self.git(module, 'config', 'core.excludesfile', str(self.base / 'ignore'))
        (self.base / 'ignore').write_text('injected.fg\n')
        (module / 'injected.fg').write_text('fn injected(): int { return 2; }\n')
        self.rejected()

    def test_security_filter_config_and_assume_unchanged_rejected(self):
        module = self.native(trusted=True)
        marker = self.base / 'filter-executed'
        self.git(module, 'config', 'filter.attack.clean', f'touch {marker}')
        self.rejected()
        self.assertFalse(marker.exists())
        self.git(module, 'config', '--remove-section', 'filter.attack')
        self.git(module, 'update-index', '--assume-unchanged', 'native.c')
        (module / 'native.c').write_text('int native(void) { return 8; }\n')
        self.rejected()

    def test_security_git_environment_cannot_redirect_verification(self):
        module = self.native(trusted=True)
        result = self.run_manager('build', extra_environment={'GIT_DIR': '/missing/git', 'GIT_WORK_TREE': '/missing/work', 'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'core.fsmonitor', 'GIT_CONFIG_VALUE_0': 'false'})
        self.assert_success(result)

    def test_security_npm_scripts_disabled_until_separately_trusted(self):
        module = self.add_module('browser-module', {'javascript': {'entry': 'index.js', 'package_file': 'package.json'}})
        (module / 'index.js').write_text('export const value = 1;\n')
        (module / 'package.json').write_text('{"name":"browser-module"}\n')
        (module / 'package-lock.json').write_text('{"lockfileVersion":3}\n')
        module = self.pin_module(module)
        self.assert_success(self.run_manager('build', '--emit-js'))
        self.assertIn('--ignore-scripts --no-bin-links', self.log.read_text())
        self.assert_success(self.run_manager('trust', 'browser-module', '--npm-scripts'))
        self.assert_success(self.run_manager('build', '--emit-js'))
        self.assertIn('--ignore-scripts=false --no-bin-links', self.log.read_text())
        project = json.loads((self.project / 'forge.json').read_text())
        self.assertTrue(project['trust']['browser-module']['npm_scripts'])
        self.assertNotIn('native', project['trust']['browser-module'])


if __name__ == '__main__':
    loader = unittest.TestLoader()
    loader.testMethodPrefix = 'test_security_'
    result = unittest.TextTestRunner(verbosity=2).run(loader.loadTestsFromTestCase(PackageSecurity))
    raise SystemExit(not result.wasSuccessful())
