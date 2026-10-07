#!/usr/bin/env python3
"""Verify installed release cache correctness, using isolated profiles and projects.

Requires an installer/download origin serving the release, cc, Node.js 22/npm,
Git, and network access to the official GitHub module repositories/npm registry.
No production credentials, HOME override, or writes outside the temporary root.
"""
import argparse
import contextlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import urllib.parse


def require(condition, message):
    if not condition:
        raise AssertionError(message)


@contextlib.contextmanager
def local_release_origin():
    """Serve only installer and release assets for CI without deploying a site."""
    repository = Path(__file__).resolve().parents[1]
    releases = (repository / 'releases').resolve()

    class Assets(http.server.BaseHTTPRequestHandler):
        def log_message(self, *arguments):
            pass

        def do_GET(self):
            path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
            if path == '/install.sh':
                file = repository / 'scripts/install.sh'
            elif path.startswith('/releases/'):
                file = (repository / path.lstrip('/')).resolve()
                if not file.is_relative_to(releases):
                    self.send_error(404)
                    return
            else:
                self.send_error(404)
                return
            if not file.is_file():
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Length', str(file.stat().st_size))
            self.end_headers()
            with file.open('rb') as stream:
                while chunk := stream.read(65536):
                    self.wfile.write(chunk)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Assets)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        yield f'http://127.0.0.1:{server.server_port}'
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--origin', default='http://localhost:18101')
    parser.add_argument('--version', default='0.3.0-preview.5')
    parser.add_argument('--local-release', action='store_true', help='Serve checked-out installer/releases on an isolated loopback port for CI')
    args = parser.parse_args()
    with contextlib.ExitStack() as resources:
        origin = resources.enter_context(local_release_origin()) if args.local_release else args.origin.rstrip('/')
        directory = resources.enter_context(tempfile.TemporaryDirectory(prefix='forge-installed-cache-'))
        root = Path(directory)
        profiles = root / 'profiles'
        profiles.mkdir()
        prefix = root / 'sdk'
        installer = root / 'install.sh'
        npm_user = root / 'npm-user.config'
        npm_global = root / 'npm-global.config'
        npm_user.write_text('')
        npm_global.write_text('')
        # Exercise the documented curl installation path; public gateways can
        # handle Python urllib differently from real installer clients.
        subprocess.run(['curl', '--fail', '--silent', '--show-error', '--location',
                        '--proto', '=https,http', '--proto-redir', '=https',
                        '--connect-timeout', '10', '--max-time', '30',
                        '--retry', '2', '--retry-delay', '1',
                        origin + '/install.sh', '-o', str(installer)],
                       check=True, timeout=120)
        env = {**os.environ, 'FORGE_PROFILE_ROOT': str(profiles), 'FORGE_HOME': str(prefix),
               'FORGE_DOWNLOAD_BASE': origin, 'FORGE_REGISTRY': 'builtin',
               'GIT_MASTER': '1', 'npm_config_cache': str(root / 'npm-cache'),
               'npm_config_userconfig': str(npm_user), 'npm_config_globalconfig': str(npm_global)}
        # Do not inherit a developer installation's compiler/runtime path.
        env.pop('FORGE_ROOT', None)
        env.pop('FORGE_BIN', None)
        forge = prefix / 'bin/forge'

        def execute(command, cwd=root, timeout=180):
            result = subprocess.run([str(part) for part in command], cwd=cwd, env=env,
                                    text=True, capture_output=True, timeout=timeout)
            require(result.returncode == 0,
                    f'Command failed: {command}\n{result.stdout}\n{result.stderr}')
            return result.stdout

        def pm(project, *arguments):
            return execute([forge, *arguments], cwd=project, timeout=300)

        def manifest(project, name, dependencies=None):
            project.mkdir()
            (project / 'forge.json').write_text(json.dumps(
                {'name': name, 'entry': 'main.fg', 'dependencies': dependencies or {}}))

        execute(['bash', installer, '--version', args.version, '--prefix', prefix, '--no-modify-path'],
                timeout=1050 if origin.startswith('https://') else 180)
        require(args.version in execute([forge, '--version']), 'Wrong installed release version')
        try:
            env['FORGE_PM'] = str(forge)
            execute([sys.executable, Path(__file__).with_name('project-init.py')])
            env.pop('FORGE_PM', None)
            native = root / 'native'
            manifest(native, 'installed-native-cache')
            (native / 'helper.fg').write_text('fn message(): string { return "native-v1"; }\n')
            (native / 'main.fg').write_text('import strings;\nimport helper;\nnative main { println(helper.message()); return 0; }\n')
            first = pm(native, 'build')
            require('Build cache hit' not in first, 'Fresh native build incorrectly hit cache')
            app = native / 'build/app'
            require(execute([app], cwd=native).strip() == 'native-v1', 'Wrong initial native output')
            require('Build cache hit' in pm(native, 'build'), 'Unchanged installed native build missed cache')
            require('native-v1' in pm(native, 'run'), 'Cached native run did not execute app')

            (native / 'helper.fg').write_text('fn message(): string { return "native-v2"; }\n')
            require('Build cache hit' not in pm(native, 'build'), 'Imported source change missed invalidation')
            require(execute([app], cwd=native).strip() == 'native-v2', 'Imported change left stale binary')
            (native / 'main.fg').write_text('import strings;\nimport helper;\nnative main { println(str_concat("changed-",helper.message())); return 0; }\n')
            require('Build cache hit' not in pm(native, 'build'), 'Entry source change missed invalidation')
            require(execute([app], cwd=native).strip() == 'changed-native-v2', 'Entry change left stale binary')

            app.chmod(0o600)
            require('Build cache hit' not in pm(native, 'build'), 'Executable mode change incorrectly hit cache')
            require(os.access(app, os.X_OK), 'Rebuild did not restore executable mode')
            for output in [native / 'build/main.c', app]:
                output.unlink()
                require('Build cache hit' not in pm(native, 'build'), f'Deleted output incorrectly hit cache: {output.name}')
                require(output.exists(), f'Deleted output was not recreated: {output.name}')
                require(execute([app], cwd=native).strip() == 'changed-native-v2', 'Regenerated output incorrect')

            # Hashing intentionally refuses symlinks: build succeeds without an
            # unsound whole-build cache key, even when the symlink is unrelated.
            target = root / 'external-input.txt'
            target.write_text('outside-project')
            (native / 'unsupported-input').symlink_to(target)
            for _ in range(2):
                require('Build cache hit' not in pm(native, 'build'), 'Symlink project incorrectly used cache')
                require(execute([app], cwd=native).strip() == 'changed-native-v2', 'Symlink disabled compilation')
            (native / 'unsupported-input').unlink()

            # Running the JavaScript target must never execute a previous
            # native artifact (including when one is present and executable).
            sentinel = root / 'native-was-executed'
            app.write_text(f'#!/bin/sh\nprintf executed > "{sentinel}"\necho NATIVE_SENTINEL\n')
            app.chmod(0o755)
            js_run = pm(native, 'run', '--emit-js')
            require(not sentinel.exists() and 'NATIVE_SENTINEL' not in js_run,
                    'run --emit-js executed the native output')
            require((native / 'build/app.js').is_file(), 'run --emit-js did not build JS output')
            require('Build cache hit' in pm(native, 'build', '--emit-js'), 'Unchanged no-deps JS build missed cache')
            require(execute(['node', native / 'build/app.js'], cwd=native).strip() == 'changed-native-v2',
                    'No-deps generated JavaScript output incorrect')
            print('Installed native cache hit/import/entry/mode/deletion/symlink and run --emit-js checks passed')

            browser = root / 'browser'
            manifest(browser, 'installed-browser-cache', {'forge-browser': '0.1.3'})
            (browser / 'helper.fg').write_text('fn message(): string { return "browser-v1"; }\n')
            (browser / 'main.fg').write_text('''import browser;
import web;
import helper;
native main {
 browser.state("cache-test","한글");
 println(helper.message());
 let value: int=web.parse("{\\"id\\":9007199254740993}");
 println(web.dump(web.get(value,"id")));
 println(str_sub(browser.state_get("cache-test"),0,3));
 if(str_len("한글")==6 && str_char_at("한글",0)==237){println("utf8-ok");}else{println("utf8-fail");}
 return 0;
}
''')
            pm(browser, 'pkg', 'install')
            lock = json.loads((browser / 'forge.lock').read_text())
            require(lock['packages']['forge-browser']['version'] == '0.1.3', 'Wrong browser module version')
            require(lock['packages']['forge-web']['version'] == '0.1.2', 'Wrong transitive web module version')
            require(all(value['repository_url'].startswith('https://github.com/forge-language/')
                        for value in lock['packages'].values()), 'Installed module points outside organization')
            require('Build cache hit' not in pm(browser, 'build', '--emit-js'), 'Fresh browser build incorrectly hit cache')
            browser_app = browser / 'build/app.js'
            expected = ['browser-v1', '9007199254740993', '한', 'utf8-ok']
            require(execute(['node', browser_app], cwd=browser).strip().splitlines() == expected,
                    'Installed browser bridge did not preserve BigInt/UTF-8')
            require('Build cache hit' in pm(browser, 'build', '--emit-js'), 'Unchanged installed browser build missed cache')
            (browser / 'helper.fg').write_text('fn message(): string { return "browser-v2"; }\n')
            require('Build cache hit' not in pm(browser, 'build', '--emit-js'), 'Browser imported change missed invalidation')
            expected[0] = 'browser-v2'
            require(execute(['node', browser_app], cwd=browser).strip().splitlines() == expected,
                    'Browser imported change left stale output')
            browser_app.write_text('throw new Error("tampered output");\n')
            require('Build cache hit' not in pm(browser, 'build', '--emit-js'), 'Tampered JS output incorrectly hit cache')
            require(execute(['node', browser_app], cwd=browser).strip().splitlines() == expected,
                    'Tampered JS output was not repaired')
            browser_app.unlink()
            require('Build cache hit' not in pm(browser, 'build', '--emit-js'), 'Deleted JS output incorrectly hit cache')
            require(execute(['node', browser_app], cwd=browser).strip().splitlines() == expected,
                    'Deleted JS output was not repaired')
            print('Installed official browser/web Git pins, JS cache invalidation, BigInt and UTF-8 checks passed')
        finally:
            if (prefix / '.forge-install').exists():
                execute(['bash', installer, '--prefix', prefix, '--uninstall'])
        require(not any(profiles.iterdir()), 'Isolated no-modify-path installation wrote profile files')
    print('Installed release cache verification passed')


if __name__ == '__main__':
    main()
