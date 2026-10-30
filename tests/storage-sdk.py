#!/usr/bin/env python3
"""Install the actual SDK and build/use the published Forge Storage library.

Requires the current release archive, forge-platform-release:storage (Ubuntu
22.04 development image) and forge-storage:local. Package artifacts come from
the SDK's builtin public registry snapshot. Uploads target a disposable local
storage container using a random fixture credential, never production secrets.
"""
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.3.0-preview.6'
ARCHIVE = ROOT / 'releases' / VERSION / ('forge-' + VERSION + '-linux-x86_64.tar.gz')


@contextlib.contextmanager
def release_origin():
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT), **kwargs)

        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path not in ['/releases/latest.json', '/releases/' + VERSION + '/' + ARCHIVE.name, '/releases/' + VERSION + '/' + ARCHIVE.name + '.sha256']:
                self.send_error(404)
                return
            super().do_GET()

        def do_HEAD(self):
            if self.path not in ['/releases/latest.json', '/releases/' + VERSION + '/' + ARCHIVE.name, '/releases/' + VERSION + '/' + ARCHIVE.name + '.sha256']:
                self.send_error(404)
                return
            super().do_HEAD()

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        yield 'http://127.0.0.1:' + str(server.server_port)
    finally:
        server.shutdown()
        server.server_close()


def main():
    expected = ARCHIVE.with_suffix(ARCHIVE.suffix + '.sha256').read_text().split()[0]
    actual = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError('Release archive and checksum do not match; finish repacking first')
    credential = secrets.token_hex(32)
    environment = dict(os.environ, STORAGE_TOKEN=credential)
    container = 'forge-storage-sdk-check-' + str(os.getpid())
    created = False
    with tempfile.TemporaryDirectory(prefix='forge-storage-sdk-check-') as temporary, release_origin() as origin:
        directory = Path(temporary)
        # Keep the example in the test tree so the installed SDK cannot see or
        # accidentally link against another checkout's build products.
        example = ROOT.parent / 'forge-storage/examples/client.fg'
        program = example.read_text()
        program = 'extern fn fs_receive_file(url: string, digest: string, path: string): int;\n' + program
        program = program.replace(' let result: int=storage.put(base,token,path);', ''' let other_hash: string=storage.digest("different-payload.bin");
 if (web.equal(hash,other_hash)!=0) { println("Digest snapshots were overwritten");return 1; }
 let result: int=storage.put(base,token,path);''')
        program = program.replace(' println("Verified download complete");', ''' if (fs_receive_file(storage.object_url(base,hash),other_hash,"mismatch-download")!=0) { println("Wrong expected digest was accepted");return 1; }
 println("Verified download complete");''')
        (directory / 'main.fg').write_text(program)
        steps = directory / 'steps.sh'
        steps.write_text('''#!/bin/bash
set -euo pipefail
mkdir -p "$SDK_TEST_ROOT/profile"
export FORGE_PROFILE_ROOT="$SDK_TEST_ROOT/profile"
bash /src/scripts/install.sh --version 0.3.0-preview.6 --prefix "$SDK_TEST_ROOT/toolchain" --no-modify-path
export PATH="$SDK_TEST_ROOT/toolchain/bin:$PATH"
cd "$SDK_TEST_ROOT"
forge init storage-app
cd storage-app
forge pkg add forge-storage 0.1.1
forge trust forge-web
forge trust forge-storage
cp "$SDK_TEST_ROOT/main.fg" main.fg
printf 'Forge SDK storage integration fixture\\n' > artifact.tar.gz
printf 'Different digest regression fixture\\n' > different-payload.bin
forge build
./build/app
cmp artifact.tar.gz downloaded-artifact.tar.gz
printf 'Installed SDK Forge Storage library integration passed\\n'
''')
        try:
            subprocess.run(['docker', 'run', '-d', '--name', container,
                            '-p', '127.0.0.1:18112:8090', '--env', 'STORAGE_TOKEN',
                            'forge-storage:local'], env=environment, check=True, capture_output=True, text=True)
            created = True
            for _ in range(60):
                try:
                    with urllib.request.urlopen('http://127.0.0.1:18112/health', timeout=1) as response:
                        if response.status == 200:
                            break
                except OSError:
                    time.sleep(.2)
            else:
                raise RuntimeError('Disposable storage server did not start')
            runner_env = dict(environment, FORGE_DOWNLOAD_BASE=origin, FORGE_REGISTRY='builtin',
                              SDK_TEST_ROOT=str(directory), STORAGE_URL='http://127.0.0.1:18112',
                              STORAGE_FILE='artifact.tar.gz')
            argv = ['docker', 'run', '--rm', '--network', 'host', '--user', str(os.getuid()) + ':' + str(os.getgid()),
                    '-v', str(ROOT) + ':/src:ro', '-v', '/tmp:/tmp', '-w', str(directory)]
            for key in ['FORGE_DOWNLOAD_BASE', 'FORGE_REGISTRY', 'SDK_TEST_ROOT', 'STORAGE_URL', 'STORAGE_FILE', 'STORAGE_TOKEN']:
                argv += ['--env', key]
            argv += ['forge-platform-release:storage', 'bash', str(steps)]
            subprocess.run(argv, env=runner_env, check=True, timeout=600)
            project = directory / 'storage-app'
            lock = json.loads((project / 'forge.lock').read_text())
            if lock['registry'] != 'builtin' or lock['packages']['forge-storage']['version'] != '0.1.1' or lock['packages']['forge-web']['version'] != '0.1.3':
                raise AssertionError('Installed library and dependency did not match the builtin source pins')
            trust = json.loads((project / 'forge.json').read_text())['trust']
            for name, value in lock['packages'].items():
                grant = trust[name]
                if grant['repository_url'] != value['repository_url'].removesuffix('.git') or grant['git_commit'] != value['git_commit'] or grant['native'] is not True:
                    raise AssertionError('Native fixture trust did not match the reviewed source pin')
            markers = {}
            for name, value in lock['packages'].items():
                cached = project / '.forge/packages' / name / value['git_commit']
                marker = cached / '.forge-artifact'
                metadata = json.loads(marker.read_text())
                if metadata['sha256'] != value['artifact']['sha256'] or metadata['git_commit'] != value['git_commit'] or (cached / '.git').exists():
                    raise AssertionError('Package cache marker is not a verified artifact')
                markers[marker] = marker.stat().st_mtime_ns
            steps.write_text('''#!/bin/bash
set -euo pipefail
export PATH="$SDK_TEST_ROOT/toolchain/bin:$PATH"
cd "$SDK_TEST_ROOT/storage-app"
forge pkg install
forge build
./build/app
cmp artifact.tar.gz downloaded-artifact.tar.gz
printf 'Repeated install and storage upload/download passed\\n'
''')
            subprocess.run(argv, env=runner_env, check=True, timeout=600)
            for marker, mtime in markers.items():
                if marker.stat().st_mtime_ns != mtime:
                    raise AssertionError('Repeated package install did not reuse its verified artifact cache')
            if (project / 'mismatch-download').exists():
                raise AssertionError('The library published a download despite a mismatched expected digest')
            if list((project / '.forge').glob('staging-*')):
                raise AssertionError('Package transport left staging files behind')
            print('Release SHA-256: ' + actual)
        finally:
            if created:
                subprocess.run(['docker', 'rm', '-f', container], check=True, capture_output=True)


if __name__ == '__main__':
    main()
