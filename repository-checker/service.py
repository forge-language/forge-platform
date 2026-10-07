#!/usr/bin/env python3
"""Bounded GitHub source inspection. Never clone, build or execute package code."""
import base64
import datetime as dt
from concurrent.futures import ThreadPoolExecutor
from collections import OrderedDict
import hashlib
import hmac
import ipaddress
import json
import os
import re
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SHA = re.compile(r'[0-9a-f]{40}\Z')
REPOSITORY = re.compile(r'https://github\.com/([A-Za-z0-9][A-Za-z0-9-]{0,38})/([A-Za-z0-9_.-]{1,100})\Z')
LIMITS = {'tree_entries': 2000, 'files': 80, 'file_bytes': 65536,
          'total_bytes': 1048576, 'seconds': 8}
SOURCE = {'.fg', '.c', '.h', '.cpp', '.rs', '.go', '.js', '.mjs', '.ts', '.py', '.sh', '.cmake'}
POLICY = 'forge-source-inspection-v1'
BLOB_CACHE = OrderedDict()
CACHE_LOCK = threading.Lock()

class Rejected(Exception):
    def __init__(self, message, status=422):
        self.message, self.status = message, status

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise Rejected('GitHub redirects are not accepted; use the canonical repository URL')


def strict_json(data):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out or '\x00' in key:
                raise Rejected('Ambiguous JSON object')
            out[key] = value
        return out
    try:
        result = json.loads(data, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(Rejected('Invalid JSON number')))
        if '\u0000' in json.dumps(result, ensure_ascii=False) or '\\u0000' in json.dumps(result):
            raise Rejected('NUL bytes are not accepted')
        return result
    except (ValueError, UnicodeError):
        raise Rejected('Invalid UTF-8 JSON')


def safe_path(value):
    return (isinstance(value, str) and 0 < len(value) <= 150
            and re.fullmatch(r'[A-Za-z0-9_.\-/]+', value) is not None
            and not value.startswith(('/', '-')) and '..' not in value
            and all(piece not in ('', '.') for piece in value.split('/')))


class GitHub:
    def __init__(self, token='', deadline=None):
        if len(token) > 512 or any(ord(c) <= 32 or ord(c) >= 127 for c in token):
            raise Rejected('Invalid GitHub token', 400)
        self.token = token
        self.deadline = deadline or time.monotonic() + LIMITS['seconds']
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def get(self, path):
        if not path.startswith('/') or '\r' in path or '\n' in path:
            raise Rejected('Invalid GitHub API path')
        immutable = '/git/blobs/' in path
        if immutable:
            with CACHE_LOCK:
                cached = BLOB_CACHE.get(path)
                if cached is not None:
                    BLOB_CACHE.move_to_end(path)
                    return cached
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise Rejected('Inspection time limit exceeded; retry later', 503)
        for entry in socket.getaddrinfo('api.github.com', 443, type=socket.SOCK_STREAM):
            if not ipaddress.ip_address(entry[4][0]).is_global:
                raise Rejected('GitHub resolved to a non-public address', 503)
        headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'Forge-repository-inspector/1',
                   'X-GitHub-Api-Version': '2022-11-28'}
        if self.token:
            headers['Authorization'] = 'Bearer ' + self.token
        request = urllib.request.Request('https://api.github.com' + path, headers=headers)
        try:
            with self.opener.open(request, timeout=min(remaining, 3)) as response:
                if response.status != 200:
                    raise Rejected('GitHub repository data unavailable', 503)
                content = response.read(4 * 1024 * 1024 + 1)
                if len(content) > 4 * 1024 * 1024:
                    raise Rejected('GitHub response exceeds inspection limits')
                result = strict_json(content)
                if immutable and len(content) <= 100000:
                    with CACHE_LOCK:
                        BLOB_CACHE[path] = result
                        while len(BLOB_CACHE) > 128:
                            BLOB_CACHE.popitem(last=False)
                return result
        except urllib.error.HTTPError as error:
            if error.code in (401, 403, 429):
                raise Rejected('GitHub authentication or rate limit prevented inspection', 503)
            raise Rejected('Repository, commit or file was not found on GitHub')
        except (urllib.error.URLError, TimeoutError, OSError):
            raise Rejected('GitHub inspection unavailable; no package was registered', 503)


def identity(token, client_factory=GitHub):
    if not isinstance(token, str) or not token:
        raise Rejected('GitHub token is required', 400)
    profile = client_factory(token).get('/user')
    login, identifier = profile.get('login'), profile.get('id')
    if (not isinstance(identifier, int) or isinstance(identifier, bool) or identifier <= 0
            or not isinstance(login, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,38}', login)):
        raise Rejected('GitHub identity could not be verified', 401)
    return {'login': login, 'github_id': str(identifier)}


def inspect(value, client_factory=GitHub):
    if not isinstance(value, dict):
        raise Rejected('Expected an object', 400)
    url = value.get('repository_url', '')
    match = REPOSITORY.fullmatch(url) if isinstance(url, str) else None
    if not match or match.group(2) in ('.', '..') or match.group(2).endswith('.git'):
        raise Rejected('Use https://github.com/OWNER/REPOSITORY without .git or a trailing slash', 400)
    path = value.get('manifest_path', 'module.json') or 'module.json'
    if not safe_path(path) or not path.endswith('.json'):
        raise Rejected('Invalid manifest path', 400)
    ref = value.get('ref', '') or ''
    if not isinstance(ref, str) or len(ref) > 150 or any(ord(c) < 33 or ord(c) > 126 for c in ref):
        raise Rejected('Invalid Git reference', 400)
    client = client_factory(os.environ.get('GITHUB_READ_TOKEN', ''))
    route = '/repos/' + match.group(1) + '/' + match.group(2)
    repo = client.get(route)
    canonical = repo.get('html_url')
    if not isinstance(canonical, str) or canonical.lower() != url.lower():
        raise Rejected('Repository was renamed or redirected; use its canonical URL')
    if repo.get('private') or repo.get('disabled') or repo.get('archived'):
        raise Rejected('Only public, active repositories may be registered')
    owner = repo.get('owner', {})
    owner_id = owner.get('id')
    if not isinstance(owner_id, int) or isinstance(owner_id, bool) or owner_id <= 0:
        raise Rejected('Repository owner identity unavailable')
    ref = ref or repo.get('default_branch', '')
    commit = client.get(route + '/commits/' + urllib.parse.quote(ref, safe=''))
    sha = commit.get('sha', '')
    if not isinstance(sha, str) or not SHA.fullmatch(sha):
        raise Rejected('GitHub did not return an immutable commit')
    tree_sha = commit.get('commit', {}).get('tree', {}).get('sha', '')
    if not isinstance(tree_sha, str) or not SHA.fullmatch(tree_sha):
        raise Rejected('Commit tree unavailable')
    tree = client.get(route + '/git/trees/' + tree_sha + '?recursive=1')
    entries = tree.get('tree')
    if tree.get('truncated') or not isinstance(entries, list) or len(entries) > LIMITS['tree_entries']:
        raise Rejected('Repository tree exceeds complete inspection limits')
    files = {}
    findings = []
    for entry in entries:
        filename = entry.get('path', '')
        if not safe_path(filename):
            raise Rejected('Unsafe repository file path')
        if entry.get('mode') in ('120000', '160000'):
            findings.append({'severity': 'high', 'rule': 'uninspected-link', 'path': filename,
                             'message': 'Symlinks and submodules are not accepted in inspected packages'})
        if entry.get('type') == 'blob':
            files[filename] = entry
    if path == 'module.json' and path not in files and 'forge.json' in files:
        path = 'forge.json'
    if path not in files or files[path].get('mode') not in ('100644', '100755'):
        raise Rejected('Manifest must be a regular file at the resolved commit')
    total = 0
    scanned = []

    def content(filename):
        nonlocal total
        entry = files[filename]
        size, blob_sha = entry.get('size'), entry.get('sha', '')
        if not isinstance(size, int) or size < 0 or size > LIMITS['file_bytes'] or not SHA.fullmatch(blob_sha):
            raise Rejected('File exceeds inspection limit: ' + filename)
        blob = client.get(route + '/git/blobs/' + blob_sha)
        if blob.get('encoding') != 'base64' or blob.get('sha') != blob_sha:
            raise Rejected('Unverifiable GitHub blob')
        try:
            raw = base64.b64decode(''.join(blob['content'].split()), validate=True)
        except (KeyError, ValueError, TypeError):
            raise Rejected('Invalid GitHub blob encoding')
        if len(raw) != size or len(raw) > LIMITS['file_bytes']:
            raise Rejected('Blob size does not match tree')
        actual_sha = hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest()
        if not hmac.compare_digest(actual_sha, blob_sha):
            raise Rejected('Blob checksum does not match commit tree')
        total += len(raw)
        if total > LIMITS['total_bytes'] or len(scanned) >= LIMITS['files']:
            raise Rejected('Source exceeds inspection limits')
        scanned.append(filename)
        try:
            return raw.decode('utf-8')
        except UnicodeError:
            raise Rejected('Inspected source must be UTF-8: ' + filename)

    manifest = strict_json(content(path))
    if not isinstance(manifest, dict):
        raise Rejected('Manifest must contain an object')
    declared = manifest.get('repository_url')
    if not isinstance(declared, str) or declared.lower() != canonical.lower():
        raise Rejected('Manifest repository does not match the requested repository')
    module = manifest.get('module')
    if not safe_path(module) or not module.endswith('.fg') or module not in files:
        raise Rejected('Declared Forge module is missing at the commit')
    manifest['repository_url'] = canonical
    manifest['git_commit'] = sha
    manifest.pop('security', None)
    manifest.pop('_inspection', None)
    manifest.pop('artifact', None)
    if 'readme' not in manifest and 'README.md' in files:
        manifest['readme'] = content('README.md')
    candidates = [name for name in files if name != path and
                  (os.path.splitext(name)[1].lower() in SOURCE or os.path.basename(name) in ('CMakeLists.txt', 'package.json'))]
    candidates.sort(key=lambda name: (name != module, name.count('/'), name))
    if len(candidates) + len(scanned) > LIMITS['files']:
        raise Rejected('Too many source files to inspect completely')
    patterns = [
        ('destructive-root', r'\brm\s+(?:-[A-Za-z]*r[A-Za-z]*\s+|-[A-Za-z]*f[A-Za-z]*\s+){1,3}(?:/\s|/\*|\$HOME\b|~(?:/|\s))', 'Destructive filesystem command'),
        ('download-execute', r'\b(?:curl|wget)\b[^\n]{0,250}\|\s*(?:ba)?sh\b', 'Downloaded content is piped to a shell'),
        ('dynamic-execution', r'\b(?:eval|exec)\s*\(\s*(?:base64|atob|bytes\.fromhex)', 'Obfuscated content is executed'),
        ('credential-exfiltration', r'(?:AWS_SECRET_ACCESS_KEY|GITHUB_TOKEN|\.ssh/id_rsa)[^\n]{0,200}(?:curl|requests\.(?:post|get)|fetch\s*\()', 'Credential access combined with network transfer'),
    ]
    def prefetch(filename):
        entry = files[filename]
        blob_sha = entry.get('sha', '')
        size = entry.get('size', LIMITS['file_bytes'] + 1)
        if not isinstance(size, int) or size > LIMITS['file_bytes'] or not SHA.fullmatch(blob_sha):
            raise Rejected('Source exceeds inspection limits: ' + filename)
        return filename, client.get(route + '/git/blobs/' + blob_sha)
    # Read immutable blobs concurrently; source is validated serially before use.
    with ThreadPoolExecutor(max_workers=4) as pool:
        prefetched = dict(pool.map(prefetch, candidates))
    original_get = client.get
    blobs = {route + '/git/blobs/' + files[name]['sha']: data for name, data in prefetched.items()}
    client.get = lambda path: blobs[path] if path in blobs else original_get(path)
    for filename in candidates:
        text = content(filename)
        for rule, expression, message in patterns:
            if re.search(expression, text, re.I):
                findings.append({'severity': 'high', 'rule': rule, 'path': filename, 'message': message})
        if filename.endswith('.fg') and re.search(r'\bextern\s+fn\b|\bos(?:_exec|\.exec)\b', text):
            findings.append({'severity': 'warning', 'rule': 'native-ffi', 'path': filename,
                             'message': 'Foreign functions can execute native code; manual review is required'})
        basename = os.path.basename(filename)
        if basename == 'package.json':
            package = strict_json(text)
            hooks = package.get('scripts', {}) if isinstance(package, dict) else {}
            if isinstance(hooks, dict) and any(key in hooks for key in ('preinstall', 'install', 'postinstall', 'prepare')):
                findings.append({'severity': 'warning', 'rule': 'npm-lifecycle', 'path': filename,
                                 'message': 'npm lifecycle scripts require separate execution trust'})
        if basename == 'CMakeLists.txt' or filename.endswith(('.cmake', '.sh')):
            findings.append({'severity': 'warning', 'rule': 'build-execution', 'path': filename,
                             'message': 'Build scripts can run arbitrary commands; review before trusting this commit'})
    if manifest.get('native') or manifest.get('javascript'):
        findings.append({'severity': 'warning', 'rule': 'native-or-javascript', 'path': module,
                         'message': 'Native and JavaScript bridges need manual code review'})
    status = ('blocked' if any(f['severity'] == 'high' for f in findings)
              else 'review_required' if findings else 'passed')
    report = {'status': status, 'policy': POLICY, 'repository': canonical, 'git_commit': sha,
              'checked_at': dt.datetime.now(dt.timezone.utc).isoformat(), 'scanned_files': len(scanned),
              'scanned_bytes': total, 'files': scanned, 'findings': findings, 'limits': LIMITS,
              'limitations': 'Static rules detect selected risks, not all malware. No source code was executed. Passing is not a safety guarantee.'}
    manifest['security'] = report
    supplied = value.get('provided_manifest')
    if supplied is not None:
        if not isinstance(supplied, dict):
            raise Rejected('Expected release manifest object', 400)
        supplied = dict(supplied)
        supplied.pop('security', None)
        supplied.pop('_inspection', None)
        supplied.pop('acknowledge_review', None)
        supplied.pop('artifact', None)
        expected = dict(manifest)
        expected.pop('security', None)
        if supplied != expected:
            raise Rejected('Submitted manifest differs from module.json at the pinned commit')
    return {'manifest': manifest, 'inspection': report, 'owner_id': str(owner_id),
            'owner_type': owner.get('type', ''), 'owner_login': owner.get('login', '')}


class Server(ThreadingHTTPServer):
    daemon_threads = True
    slots = threading.BoundedSemaphore(4)
    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            request.sendall(b'HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            self.shutdown_request(request)
            return
        super().process_request(request, client_address)
    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # Never log bearer credentials or repository request bodies.
    def setup(self):
        super().setup()
        self.connection.settimeout(10)
    def respond(self, status, body):
        data = json.dumps(body, ensure_ascii=True).encode("ascii")
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        self.respond(200 if self.path == '/health' else 404, {'status': 'ok' if self.path == '/health' else 'not_found'})
    def do_POST(self):
        expected = os.environ.get('REPOSITORY_CHECK_TOKEN', '')
        if not expected or not hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + expected):
            return self.respond(401, {'error': 'unauthorized'})
        try:
            if self.path not in ('/inspect', '/identity'):
                raise Rejected('Unknown endpoint', 404)
            if self.headers.get('Transfer-Encoding') or len(self.headers.get_all('Content-Length', [])) != 1:
                raise Rejected('A single bounded Content-Length is required', 400)
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 65536:
                raise Rejected('Request too large', 413)
            value = strict_json(self.rfile.read(size))
            if not isinstance(value, dict):
                raise Rejected('Expected object', 400)
            result = identity(value.get('token')) if self.path == '/identity' else inspect(value)
            self.respond(200, result)
        except Rejected as error:
            self.respond(error.status, {'error': 'repository_rejected', 'message': error.message})
        except (ValueError, KeyError, TypeError):
            self.respond(400, {'error': 'invalid_request', 'message': 'Invalid request data'})
        except Exception:
            self.respond(503, {'error': 'inspection_unavailable', 'message': 'Repository inspection unavailable; no registration was performed'})

if __name__ == '__main__':
    if len(os.environ.get('REPOSITORY_CHECK_TOKEN', '')) < 24:
        raise SystemExit('REPOSITORY_CHECK_TOKEN must be configured with at least 24 characters')
    Server(('0.0.0.0', 8090), Handler).serve_forever()
