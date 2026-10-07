#!/usr/bin/env python3
"""Publish selected SDK archives while preserving existing Storage redirects."""
import argparse
import datetime
import email.utils
import hashlib
import http.client
import json
import pathlib
import re
import time
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAX_OBJECT = 256 * 1024 * 1024
VERSION = re.compile(r'[0-9][A-Za-z0-9.-]{0,63}')
ORIGIN = re.compile(r'https://[A-Za-z0-9.-]+(?::[0-9]+)?')
DIGEST = re.compile(r'[0-9a-f]{64}')


def version(value):
    if not VERSION.fullmatch(value) or '..' in value or value.endswith('.'):
        raise argparse.ArgumentTypeError('Invalid release version')
    return value


def validate_record(path, metadata):
    if not isinstance(path, str) or not isinstance(metadata, dict):
        raise ValueError('Invalid artifact record')
    parts = path.split('/')
    if len(parts) != 4 or parts[:2] != ['', 'releases']:
        raise ValueError('Invalid artifact path')
    version(parts[2])
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,159}\.tar\.gz(?:\.sha256)?', parts[3]):
        raise ValueError('Invalid artifact filename')
    digest, url = metadata.get('sha256'), metadata.get('url')
    if not isinstance(digest, str) or not DIGEST.fullmatch(digest) or not isinstance(url, str):
        raise ValueError('Invalid artifact digest or URL')
    origin, separator, suffix = url.partition('/v1/objects/')
    if not separator or not ORIGIN.fullmatch(origin) or suffix != digest:
        raise ValueError('Invalid artifact object URL')
    result = {'sha256': digest, 'url': url}
    if 'size' in metadata:
        size = metadata['size']
        if type(size) is not int or not 0 <= size <= MAX_OBJECT:
            raise ValueError('Invalid artifact size')
        result['size'] = size
    return result


def existing_records(releases):
    records = {}
    manifest = releases / 'artifacts.json'
    if manifest.exists():
        if manifest.stat().st_size > 1024 * 1024:
            raise ValueError('Artifact manifest exceeds limit')
        data = json.loads(manifest.read_text())
        if not isinstance(data, dict):
            raise ValueError('Invalid artifact manifest')
        records = {path: validate_record(path, item) for path, item in data.items()}
    config = releases / 'artifacts.conf'
    if config.exists():
        if config.stat().st_size > 1024 * 1024:
            raise ValueError('Artifact config exceeds limit')
        lines = config.read_text().strip().splitlines()
        if not lines or lines[0].strip() != 'map $uri $forge_release_url {' or lines[-1].strip() != '}':
            raise ValueError('Invalid artifact config')
        for line in lines[1:-1]:
            if line.strip() == 'default "";':
                continue
            match = re.fullmatch(r'\s*"([^"]+)" "([^"]+)";\s*', line)
            if not match:
                raise ValueError('Invalid artifact redirect')
            path, url = match.groups()
            item = validate_record(path, {'url': url, 'sha256': url.rsplit('/', 1)[-1]})
            if path in records and records[path]['url'] != url:
                raise ValueError('Conflicting artifact redirect')
            records.setdefault(path, item)
    return records


def retry_delay(header, attempt):
    delay = 2 ** attempt
    if header:
        try:
            delay = float(header)
        except ValueError:
            try:
                when = email.utils.parsedate_to_datetime(header)
                delay = (when - datetime.datetime.now(datetime.timezone.utc)).total_seconds()
            except (TypeError, ValueError, OverflowError):
                pass
    return max(0, min(10, delay))


def upload(base, token, file):
    size = file.stat().st_size
    if size > MAX_OBJECT:
        raise ValueError('Object exceeds 256 MiB')
    digest = hashlib.sha256()
    with file.open('rb') as stream:
        while chunk := stream.read(65536):
            digest.update(chunk)
    key = digest.hexdigest()
    url = urllib.parse.urlsplit(base)
    if url.scheme not in ('http', 'https') or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError('Invalid storage origin')
    for attempt in range(4):
        conn = (http.client.HTTPSConnection if url.scheme == 'https' else http.client.HTTPConnection)(url.hostname, url.port, timeout=180)
        try:
            conn.putrequest('PUT', url.path.rstrip('/') + '/v1/objects/' + key)
            conn.putheader('Authorization', 'Bearer ' + token)
            conn.putheader('Content-Type', 'application/octet-stream')
            conn.putheader('Content-Length', str(size))
            conn.endheaders()
            with file.open('rb') as stream:
                while chunk := stream.read(65536):
                    conn.send(chunk)
            response = conn.getresponse()
            status, retry_after = response.status, response.getheader('Retry-After')
            body = response.read(8192)
        finally:
            conn.close()
        if status in (429, 503) and attempt < 3:
            time.sleep(retry_delay(retry_after, attempt))
            continue
        if status not in (200, 201):
            raise RuntimeError('Storage returned HTTP ' + str(status))
        result = json.loads(body)
        if not isinstance(result, dict) or result.get('sha256') != key or type(result.get('size')) is not int or result['size'] != size:
            raise RuntimeError('Storage metadata mismatch')
        return {'sha256': key, 'size': size}


def publish(base, public, token, selected=None):
    if not ORIGIN.fullmatch(public):
        raise ValueError('Invalid public origin')
    releases = ROOT / 'releases'
    records = existing_records(releases)
    files = sorted((releases / version(selected)).glob('*')) if selected else sorted(releases.glob('*/*'))
    files = [file for file in files if file.name.endswith(('.tar.gz', '.tar.gz.sha256'))]
    if not files:
        raise ValueError('No release archives found')
    published = 0
    for file in files:
        if file.is_symlink() or not file.is_file() or not file.resolve().is_relative_to(releases.resolve()):
            raise ValueError('Invalid release archive')
        path = '/releases/' + file.relative_to(releases).as_posix()
        # Validate every route before making any credentialed request.
        validate_record(path, {'sha256': '0' * 64, 'url': public + '/v1/objects/' + '0' * 64})
    for file in files:
        metadata = upload(base, token, file)
        metadata['url'] = public + '/v1/objects/' + metadata['sha256']
        records['/releases/' + file.relative_to(releases).as_posix()] = metadata
        published += 1
    records = dict(sorted(records.items()))
    target = releases / 'artifacts.conf'
    temporary = target.with_suffix('.tmp')
    temporary.write_text('map $uri $forge_release_url {\n default "";\n' + ''.join(' "' + path + '" "' + value['url'] + '";\n' for path, value in records.items()) + '}\n')
    temporary.replace(target)
    target = releases / 'artifacts.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(records, indent=2) + '\n')
    temporary.replace(target)
    return published


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', default='http://127.0.0.1:18104')
    parser.add_argument('--public', default='https://storage.forge-lang.org')
    parser.add_argument('--version', type=version, help='Upload only this release; preserve previous redirects')
    args = parser.parse_args()
    env = dict(line.split('=', 1) for line in (ROOT / '.env').read_text().splitlines() if '=' in line and not line.startswith('#'))
    count = publish(args.base, args.public, env['STORAGE_TOKEN'], args.version)
    print('Published', count, 'SDK objects; reload the site nginx after validating its configuration.')


if __name__ == '__main__':
    main()
