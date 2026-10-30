#!/usr/bin/env python3
"""Real GitHub-archive -> registry -> private Forge Storage integration.

Creates a disposable DB, Redis and object store, publishes an official manifest
without artifacts, then tests opt-in startup backfill and authenticated publish.
Network access to the official GitHub codeload origin is required. No production
credentials, containers, object volumes or public DNS are used or changed.
"""
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = 'http://127.0.0.1:18109'
OBJECTS = 'http://127.0.0.1:18111'
SECRET = 'registry-storage-integration-only'


def jwt():
    def encode(value):
        return base64.urlsafe_b64encode(json.dumps(value).encode()).rstrip(b'=')
    data = encode({'alg': 'HS256'}) + b'.' + encode({'sub': 'Helloworld0822', 'github_id': '1', 'role': 'admin', 'exp': int(time.time()) + 3600})
    return (data + b'.' + base64.urlsafe_b64encode(hmac.new(SECRET.encode(), data, hashlib.sha256).digest()).rstrip(b'=')).decode()


def request(path, value=None):
    data = json.dumps(value).encode() if value is not None else None
    headers = {'Content-Type': 'application/json'}
    if data is not None:
        headers['Authorization'] = 'Bearer ' + jwt()
    query = urllib.request.Request(BASE + path, data=data, headers=headers)
    try:
        response = urllib.request.urlopen(query, timeout=240)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, json.loads(response.read())


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    project = 'forge-registry-storage-check-' + str(os.getpid())
    with tempfile.TemporaryDirectory(prefix='forge-registry-storage-check-') as temp:
        directory = Path(temp)
        envfile = directory / 'config.env'
        envfile.write_text('\n'.join([
            'POSTGRES_PASSWORD=registry-storage-test-password', 'JWT_SECRET=' + SECRET,
            'SITE_PORT=18112', 'ADMIN_GITHUB_ID=1', 'FRONTEND_URL=' + BASE, 'BACKEND_BASE_URL=' + BASE,
            'FORGE_PROXY_SUBNET=172.28.245.0/29', 'FORGE_PROXY_IP=172.28.245.3',
        ]) + '\n')
        override = directory / 'override.yml'
        command = ['docker', 'compose', '--env-file', str(envfile), '-p', project,
                   '-f', str(ROOT / 'compose.yml'), '-f', str(ROOT / 'tests/compose.yml'), '-f', str(override)]

        def compose(*args, capture=False):
            return subprocess.run(command + list(args), cwd=ROOT, check=True,
                                  text=True, capture_output=capture)

        def configure(enabled, backfill):
            override.write_text('''services:
 api:
  image: ''' + os.environ.get('FORGE_STORAGE_TEST_API_IMAGE', 'forge-platform-backend:storage-test') + '''
  environment:
   STORAGE_BASE_URL: "''' + ('http://storage:8090' if enabled else '') + '''"
   STORAGE_PUBLIC_URL: "''' + OBJECTS + '''"
   STORAGE_TOKEN: registry-storage-integration-token-0123456789
   STORAGE_BACKFILL_ON_START: "''' + str(backfill) + '''"
  ports: ["127.0.0.1:18109:8080"]
  healthcheck:
   start_period: 240s
 storage:
  image: forge-storage:local
  environment:
   STORAGE_TOKEN: registry-storage-integration-token-0123456789
   STORAGE_ROOT: /data
   PORT: 8090
  volumes: [objects-test:/data]
  ports: ["127.0.0.1:18111:8090"]
  restart: "no"
volumes:
 objects-test:
''')

        configure(False, 0)
        try:
            compose('up', '-d', '--no-build', '--wait', '--wait-timeout', '240', 'api', 'storage')
            official = next(item for item in json.loads((ROOT / 'backend/seed.json').read_text()) if item['name'] == 'forge-web')
            official.pop('artifact', None)
            official['acknowledge_review'] = True
            legacy_input = {'repository_url': official['repository_url'], 'ref': official['git_commit'], 'acknowledge_review': True}
            status, legacy = request('/api/packages/register', legacy_input)
            require(status == 201 and not legacy.get('artifact'), 'Legacy fixture publish failed: ' + str((status, legacy)))
            before = request('/api/packages/forge-web')[1]['versions'][0]
            configure(True, 1)
            compose('up', '-d', '--no-build', '--force-recreate', '--wait', '--wait-timeout', '240', 'api')
            status, enriched = request('/api/packages/forge-web/versions/' + official['version'])
            require(status == 200 and enriched.get('artifact'), 'Startup backfill did not attach artifact')
            require(enriched['git_commit'] == official['git_commit'] and enriched['version'] == official['version'], 'Backfill changed source pin/version')
            after = request('/api/packages/forge-web')[1]['versions'][0]
            require(before['published_at'] == after['published_at'], 'Backfill changed publication date')
            artifact = enriched['artifact']
            with urllib.request.urlopen(artifact['url'], timeout=30) as response:
                data = response.read()
            require(len(data) == artifact['size'], 'Artifact size mismatch')
            require(hashlib.sha256(data).hexdigest() == artifact['sha256'], 'Artifact digest mismatch')
            compose('up', '-d', '--no-build', '--force-recreate', '--wait', '--wait-timeout', '240', 'api')
            require(request('/api/packages/forge-web/versions/' + official['version'])[1] == enriched, 'Backfill is not idempotent')
            # The checker reads the authoritative package name/version from GitHub.
            # Publish a second actual official repository, never a fabricated version.
            candidate = next(item for item in json.loads((ROOT / 'backend/seed.json').read_text()) if item['name'] == 'forge-postgres')
            candidate['acknowledge_review'] = True
            candidate['artifact'] = {'url': 'https://untrusted.invalid/injected', 'sha256': '0' * 64, 'size': 1}
            registration = {'repository_url': candidate['repository_url'], 'ref': candidate['git_commit'], 'acknowledge_review': True, 'artifact': candidate['artifact']}
            status, published = request('/api/packages/register', registration)
            require(status == 201, 'Storage-backed publication failed: ' + str((status, published)))
            require(published['name'] == candidate['name'] and published['version'] == candidate['version'] and published['git_commit'] == candidate['git_commit'], 'Server changed authoritative source identity')
            require(published['artifact']['url'] != candidate['artifact']['url'], 'Server accepted a caller-supplied artifact URL')
            with urllib.request.urlopen(published['artifact']['url'], timeout=30) as response:
                published_data = response.read()
            require(len(published_data) == published['artifact']['size'] and hashlib.sha256(published_data).hexdigest() == published['artifact']['sha256'], 'Second source-derived artifact verification failed')
            require(request('/api/packages/register', registration)[0] == 409, 'Immutable version was overwritten')
            print('Storage integration passed: real archive publication, SHA-256/size download, immutable versions, startup backfill, source/date preservation, idempotence and server-owned artifact URL.')
        finally:
            compose('down', '-v', '--remove-orphans')


if __name__ == '__main__':
    main()
