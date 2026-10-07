#!/usr/bin/env python3
"""Refresh public Forge metadata and evidence without running repository code."""
import argparse
import base64
import datetime
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
ORG = 'forge-language'
REPOSITORIES = frozenset(['forge', 'forge-runtime', 'forge-stdlib', 'language-server',
    'vscode-extension', 'editor-configs', 'forge-proofs', 'forge-benchmarks',
    'forge-platform', 'forge-web', 'forge-postgres', 'forge-browser', 'forge-storage'])
DOCUMENTS = ('README.md', 'ARCHITECTURE.md', 'LANGUAGE_SPEC.md', 'ROADMAP.md',
             'CONTRIBUTING.md', 'CODE_OF_CONDUCT.md', 'RFC_PROCESS.md', 'GOOD_FIRST_ISSUES.md')
MAX_FILE = 4 * 1024 * 1024
MAX_TOTAL = 24 * 1024 * 1024
MAX_SNAPSHOTS = 64
SNAPSHOT_MAX_AGE = 7 * 24 * 3600
SHA = re.compile(r'^[0-9a-f]{40}$')
HASH = re.compile(r'^[0-9a-f]{64}$')


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode()


def safe_path(value):
    if not isinstance(value, str) or not value or len(value) > 220:
        raise ValueError('Invalid public file path')
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ('', '.', '..') for part in value.split('/')):
        raise ValueError('Invalid public file path')
    if not re.fullmatch(r'[A-Za-z0-9_./-]+', value):
        raise ValueError('Invalid public file path')
    return value


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('Redirects are not permitted for public source fetches')


class Client:
    def __init__(self, cache, token=None):
        self.path = Path(cache)
        self.token = token
        self.opener = urllib.request.build_opener(NoRedirect())
        try:
            self.cache = json.loads(self.path.read_text())
        except (OSError, ValueError):
            self.cache = {'responses': {}, 'api_requests': []}
        self.cache.setdefault('responses', {})
        self.cache.setdefault('api_requests', [])

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        atomic_file(self.path, encoded(self.cache), mode=0o600)

    def get(self, url, ttl=60, immutable=False):
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != 'https' or parsed.username or parsed.password or parsed.fragment or parsed.port not in (None, 443):
            raise ValueError('Only allowlisted HTTPS source URLs are permitted')
        if parsed.hostname == 'api.github.com':
            if not (parsed.path.startswith('/orgs/' + ORG + '/repos') or
                    any(parsed.path.startswith('/repos/' + ORG + '/' + repo + '/')
                        for repo in REPOSITORIES)):
                raise ValueError('API URL outside the public organization allowlist')
        elif parsed.hostname == 'raw.githubusercontent.com':
            parts = parsed.path.strip('/').split('/')
            if len(parts) < 4 or parts[0] != ORG or parts[1] not in REPOSITORIES:
                raise ValueError('Raw URL outside the public organization allowlist')
            safe_path('/'.join(parts[3:]))
        else:
            raise ValueError('Source host is not allowlisted')
        cached = self.cache['responses'].get(url)
        current = time.time()
        if cached and (immutable or current - cached['checked_at'] < ttl):
            return base64.b64decode(cached['body'])
        headers = {'User-Agent': 'Forge-public-refresh', 'Accept': 'application/vnd.github+json'}
        if cached and cached.get('etag'):
            headers['If-None-Match'] = cached['etag']
        if parsed.hostname == 'api.github.com':
            requests = [stamp for stamp in self.cache['api_requests'] if current - stamp < 3600]
            if len(requests) >= (120 if self.token else 40):
                raise ValueError('Public API request budget exhausted; retaining previous snapshot')
            requests.append(current)
            self.cache['api_requests'] = requests
            if self.token:
                headers['Authorization'] = 'Bearer ' + self.token
        request = urllib.request.Request(url, headers=headers)
        try:
            with self.opener.open(request, timeout=10) as response:
                if response.status != 200:
                    raise ValueError('Source response was not successful')
                body = response.read(MAX_FILE + 1)
                if len(body) > MAX_FILE:
                    raise ValueError('Public source file exceeds size limit')
                self.cache['responses'][url] = {'checked_at': current, 'etag': response.headers.get('ETag'),
                                                'body': base64.b64encode(body).decode()}
                return body
        except urllib.error.HTTPError as error:
            if error.code == 304 and cached:
                cached['checked_at'] = current
                return base64.b64decode(cached['body'])
            raise ValueError('Public source returned HTTP ' + str(error.code)) from None

    def api(self, path, ttl=300):
        return json.loads(self.get('https://api.github.com' + path, ttl=ttl))

    def raw(self, repository, revision, path, immutable=True):
        if repository not in REPOSITORIES or (immutable and not SHA.fullmatch(revision)):
            raise ValueError('Invalid source repository or immutable revision')
        safe_path(path)
        return self.get(f'https://raw.githubusercontent.com/{ORG}/{repository}/{revision}/{path}',
                        ttl=60, immutable=immutable)


def atomic_file(path, data, mode=0o644):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            os.fchmod(output.fileno(), mode)
            output.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def metadata(items):
    if not isinstance(items, list) or len(items) > 100:
        raise ValueError('Malformed public repository list')
    result = []
    for item in items:
        name = item.get('name')
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or item.get('private') or item.get('owner', {}).get('login') != ORG:
            continue
        if item.get('html_url') != f'https://github.com/{ORG}/{name}':
            raise ValueError('Unexpected repository URL')
        counts = [item.get(field) for field in ['stargazers_count', 'forks_count', 'open_issues_count']]
        if any(type(value) is not int or value < 0 for value in counts):
            raise ValueError('Malformed repository counters')
        result.append({'name': name, 'url': item['html_url'], 'description': item.get('description') or '',
                       'default_branch': item.get('default_branch', 'main'), 'pushed_at': item.get('pushed_at'),
                       'stars': counts[0], 'forks': counts[1], 'open_items': counts[2],
                       'archived': bool(item.get('archived'))})
    if not any(item['name'] == 'forge' for item in result):
        raise ValueError('Canonical compiler repository missing from public list')
    return sorted(result, key=lambda item: item['name'])


def pulls(items):
    if not isinstance(items, list) or len(items) > 20:
        raise ValueError('Malformed pull request list')
    result = []
    for item in items:
        number = item.get('number')
        if type(number) is not int or number < 1 or item.get('html_url') != f'https://github.com/{ORG}/forge/pull/{number}':
            raise ValueError('Unexpected pull request URL')
        result.append({'number': number, 'title': item['title'], 'url': item['html_url'],
                       'state': item['state'], 'merged_at': item.get('merged_at'),
                       'author': item['user']['login'], 'labels': [label['name'] for label in item['labels']]})
    return result


def validate_benchmark(report):
    if not isinstance(report, dict) or report.get('schema_version') != 1:
        raise ValueError('Malformed benchmark report schema')
    revisions = report.get('source_revisions', {})
    if set(revisions) != {'forge', 'forge-runtime', 'forge-stdlib', 'forge-benchmarks'} or any(not isinstance(value, str) or not SHA.fullmatch(value) for value in revisions.values()):
        raise ValueError('Benchmark report lacks exact source revisions')
    if report.get('checks') != {'scheduler_completed': True, 'string_checksums': True, 'parser_declarations': True}:
        raise ValueError('Benchmark workload validation failed')
    if not isinstance(report.get('summary'), dict) or not isinstance(report.get('runs'), dict) or not report.get('measured_at'):
        raise ValueError('Benchmark report lacks measured observations')
    repeats = report.get('repeats')
    if type(repeats) is not int or not 3 <= repeats <= 20:
        raise ValueError('Invalid measurement repeat count')
    observations = report['runs']
    for group in ['scheduler', 'strings', 'parser']:
        if not isinstance(observations.get(group), list) or not observations[group]:
            raise ValueError('Benchmark report lacks raw samples')
    if len(observations['parser']) != repeats or len(observations['scheduler']) != repeats * 3:
        raise ValueError('Incomplete benchmark repeat observations')
    if {(row.get('case'), row.get('repeat')) for row in observations['scheduler']} != {(case, repeat) for case in ['dispatch_10000_1', 'dispatch_10000_4', 'idle_50ms'] for repeat in range(1, repeats + 1)}:
        raise ValueError('Duplicate or missing scheduler repeat case')
    if {row.get('repeat') for row in observations['parser']} != set(range(1, repeats + 1)):
        raise ValueError('Duplicate or missing parser repeat')
    for row in observations['parser']:
        if row.get('declarations') != 100000 or row.get('iterations') != 15:
            raise ValueError('Invalid parser fixture completion')
    for row in observations['scheduler']:
        if str(row.get('case', '')).startswith('dispatch') and (row.get('completed') != 10000 or row.get('coroutines') != 10000):
            raise ValueError('Invalid scheduler fixture completion')
    for group, metrics in [('scheduler', ['wall_ms', 'process_cpu_ms']), ('strings', ['seconds']), ('parser', ['parse_seconds'])]:
        for row in observations[group]:
            if any(type(row.get(key)) not in (int, float) or not math.isfinite(row[key]) or row[key] < 0 for key in metrics):
                raise ValueError('Nonfinite benchmark observation')
    expected_strings = {(operation, size, implementation, repeat)
        for operation, sizes, implementations in [
            ('scan', [4096, 16384, 65536], ['legacy', 'view']),
            ('append', [4096, 16384], ['legacy', 'builder']),
            ('append_slice', [4096, 16384, 65536], ['substring', 'view']),
            ('match', [4096, 16384, 65536], ['substring', 'view'])]
        for size in sizes for implementation in implementations for repeat in range(1, repeats + 1)}
    actual_strings = [(row.get('operation'), row.get('bytes'), row.get('implementation'), row.get('repeat')) for row in observations['strings']]
    if len(actual_strings) != len(expected_strings) or set(actual_strings) != expected_strings:
        raise ValueError('Duplicate or missing string workload observations')
    checksum_groups = {}
    for row in observations['strings']:
        if type(row.get('checksum')) is not int or row['checksum'] < 0 or row.get('repeat') not in range(1, repeats + 1):
            raise ValueError('Invalid string workload observation')
        key = (row.get('operation'), row.get('bytes'))
        checksum_groups.setdefault(key, set()).add(row.get('checksum'))
    if any(len(values) != 1 for values in checksum_groups.values()):
        raise ValueError('String workload checksums disagree')
    if report.get('source_tree_clean') != {name: True for name in ['forge', 'forge-runtime', 'forge-stdlib', 'forge-benchmarks']}:
        raise ValueError('Published measurement source tree is dirty')
    run_url = report.get('run_url', '')
    if run_url and not re.fullmatch(r'https://github.com/forge-language/forge-benchmarks/actions/runs/[0-9]+', run_url):
        raise ValueError('Unexpected benchmark workflow provenance URL')
    return report


def previous_snapshot(output):
    current = output / 'current'
    try:
        manifest = json.loads((current / 'manifest.json').read_text())
        if manifest.get('schema_version') != 1:
            return {}, {}
        files = {}
        for group in ['documents', 'reports']:
            for item in manifest.get(group, []):
                path = safe_path(item['path'])
                body = (current / path).read_bytes()
                if hashlib.sha256(body).hexdigest() != item['sha256']:
                    raise ValueError('Previous content checksum failed')
                files[path] = body
        if manifest.get('latest_benchmark', {}).get('status') == 'available':
            files['benchmarks/latest.json'] = (current / 'benchmarks/latest.json').read_bytes()
        return manifest, files
    except (OSError, ValueError, KeyError):
        return {}, {}


def local_content(content):
    files, groups = {}, {'documents': [], 'reports': []}
    for kind, group in [('project', 'documents'), ('reports', 'reports')]:
        for path in sorted((content / kind).glob('*')):
            if not path.is_file() or path.suffix not in ('.md', '.json'):
                continue
            relative = group + '/' + safe_path(path.name)
            body = path.read_bytes()
            if len(body) > MAX_FILE:
                raise ValueError('Local evidence exceeds size limit')
            files[relative] = body
            item = {'title': path.stem.replace('-', ' '), 'path': relative,
                    'sha256': hashlib.sha256(body).hexdigest(), 'repository': 'forge' if group == 'documents' else 'forge-benchmarks',
                    'commit': None, 'provenance': 'checked-in historical snapshot'}
            if group == 'reports':
                date = re.search(r'20\d\d-\d\d-\d\d', path.name)
                item['date'] = date.group() if date else None
            groups[group].append(item)
    return groups, files


def refresh(output, content, client=None, offline=False):
    output, content = Path(output), Path(content)
    output.mkdir(parents=True, exist_ok=True)
    output.chmod(0o755)
    groups, local_files = local_content(content)
    old, files = previous_snapshot(output)
    if old:
        result = {key: old.get(key, default) for key, default in
                  [('repositories', []), ('activity', {}), ('documents', []), ('reports', []),
                   ('source_revisions', {}), ('source_timestamps', {}), ('report_source_revisions', {}), ('report_source_timestamps', {}), ('latest_benchmark', {'status': 'unavailable'})]}
    else:
        files = dict(local_files)
        result = {**groups, 'repositories': [], 'activity': {}, 'source_revisions': {}, 'source_timestamps': {}, 'report_source_revisions': {}, 'report_source_timestamps': {},
                  'latest_benchmark': {'status': 'unavailable'}}
    errors = []
    if not offline:
        def attempt(component, action):
            try:
                action()
            except (ValueError, KeyError, TypeError, AttributeError, StopIteration, OSError, urllib.error.URLError) as error:
                # No request headers, tokens, or arbitrary response bodies in logs/manifests.
                errors.append({'component': component, 'message': type(error).__name__ + ': public refresh failed; previous data retained'})

        def repository_data():
            result['repositories'] = metadata(client.api('/orgs/forge-language/repos?type=public&per_page=100'))
        attempt('repositories', repository_data)

        def activity_data():
            compiler = next(item for item in result['repositories'] if item['name'] == 'forge')
            records = pulls(client.api('/repos/forge-language/forge/pulls?state=all&sort=updated&direction=desc&per_page=20', ttl=600))
            result['activity'] = {'updated_at': now(), 'repository': compiler['url'],
                'stats': {key: compiler[key] for key in ['stars', 'forks', 'open_items']}, 'pulls': records}
        attempt('activity', activity_data)

        for repository, group in [('forge', 'documents'), ('forge-benchmarks', 'reports')]:
            def content_data(repository=repository, group=group):
                present = next(item for item in result['repositories'] if item['name'] == repository)
                previous = next((item for item in old.get('repositories', []) if item['name'] == repository), {})
                if present.get('pushed_at') == result['source_timestamps'].get(repository) and result['source_revisions'].get(repository):
                    return
                branch = present['default_branch']
                if not re.fullmatch(r'[A-Za-z0-9_.-]+', branch):
                    raise ValueError('Invalid source branch')
                revision = client.api(f'/repos/{ORG}/{repository}/commits/{branch}')['sha']
                if not SHA.fullmatch(revision):
                    raise ValueError('Invalid source commit')
                if revision == result['source_revisions'].get(repository):
                    result['source_timestamps'][repository] = present.get('pushed_at')
                    return
                if group == 'documents':
                    paths = list(DOCUMENTS)
                    # Only advertised existing source files are fetched; optional documents can be absent.
                    tree = client.api(f'/repos/{ORG}/{repository}/git/trees/{revision}?recursive=1')
                    existing = {item['path'] for item in tree['tree'] if item.get('type') == 'blob'}
                    paths = [path for path in paths if path in existing]
                else:
                    tree = client.api(f'/repos/{ORG}/{repository}/git/trees/{revision}?recursive=1')
                    if tree.get('truncated'):
                        raise ValueError('Source document tree truncated')
                    paths = [item['path'] for item in tree['tree'] if item.get('type') == 'blob'
                             and item['path'].startswith('docs/') and item['path'].endswith(('.md', '.json'))]
                if not paths or len(paths) > 64:
                    raise ValueError('Unexpected source document inventory')
                pending, records = {}, []
                for source in sorted(paths):
                    body = client.raw(repository, revision, safe_path(source))
                    if source.endswith('.json'):
                        json.loads(body)
                    path = group + '/' + PurePosixPath(source).name
                    if path in pending:
                        raise ValueError('Duplicate document basename')
                    pending[path] = body
                    date = re.search(r'20\d\d-\d\d-\d\d', source)
                    item = {'title': PurePosixPath(source).stem.replace('-', ' '), 'path': path,
                            'repository': repository, 'commit': revision, 'source_path': source, 'sha256': hashlib.sha256(body).hexdigest()}
                    if group == 'reports':
                        item['date'] = date.group() if date else None
                    records.append(item)
                preserved_records = [item for item in result[group] if group == 'reports'
                    and item.get('commit') and item.get('repository') != repository]
                preserved_files = {item['path']: files[item['path']] for item in preserved_records}
                # Commit a group only after its entire remote inventory was validated.
                for path in list(files):
                    if path.startswith(group + '/'):
                        del files[path]
                files.update(pending)
                files.update(preserved_files)
                records.extend(preserved_records)
                if group == 'documents':
                    historical = [item for item in groups[group] if item['path'] not in pending]
                    for item in historical:
                        files[item['path']] = local_files[item['path']]
                    records.extend(historical)
                result[group] = records
                result['source_revisions'][repository] = revision
                result['source_timestamps'][repository] = present.get('pushed_at')
            attempt(group, content_data)

        # Additional public reports preserve the existing site's material URLs.
        # Content repositories remain fixed; arbitrary new org repos get metadata only.
        for repository in ['forge', 'forge-platform', 'forge-storage']:
            def extra_reports(repository=repository):
                present = next((item for item in result['repositories'] if item['name'] == repository), None)
                if present is None:
                    return
                if result['report_source_timestamps'].get(repository) == present.get('pushed_at'):
                    return
                branch = present['default_branch']
                if not re.fullmatch(r'[A-Za-z0-9_.-]+', branch):
                    raise ValueError('Invalid public report source branch')
                revision = result['source_revisions'].get('forge') if repository == 'forge' else None
                revision = revision or client.api(f'/repos/{ORG}/{repository}/commits/{branch}')['sha']
                if not SHA.fullmatch(revision):
                    raise ValueError('Invalid public report source commit')
                if result['report_source_revisions'].get(repository) == revision:
                    result['report_source_timestamps'][repository] = present.get('pushed_at')
                    return
                tree = client.api(f'/repos/{ORG}/{repository}/git/trees/{revision}?recursive=1')
                if tree.get('truncated'):
                    raise ValueError('Additional public report tree truncated')
                paths = [entry['path'] for entry in tree['tree'] if entry.get('type') == 'blob'
                         and entry['path'].endswith(('.md', '.json'))
                         and re.search(r'(benchmark|performance|(?:^|/)reports?/)', entry['path'], re.I)
                         and not any(part.startswith('.') for part in entry['path'].split('/'))]
                if len(paths) > 32:
                    raise ValueError('Additional public report inventory exceeds limit')
                pending, records = {}, []
                for source in sorted(paths):
                    source = safe_path(source)
                    body = client.raw(repository, revision, source)
                    if source.endswith('.json'):
                        json.loads(body)
                    path = 'reports/' + repository + '/' + source
                    pending[path] = body
                    date = re.search(r'20\d\d-\d\d-\d\d', source)
                    records.append({'title': PurePosixPath(source).stem.replace('-', ' '), 'path': path,
                        'date': date.group() if date else None, 'repository': repository, 'commit': revision,
                        'source_path': source, 'sha256': hashlib.sha256(body).hexdigest()})
                prefix = 'reports/' + repository + '/'
                for path in list(files):
                    if path.startswith(prefix):
                        del files[path]
                files.update(pending)
                result['reports'] = [record for record in result['reports'] if not record['path'].startswith(prefix)] + records
                result['report_source_revisions'][repository] = revision
                result['report_source_timestamps'][repository] = present.get('pushed_at')
            attempt('reports:' + repository, extra_reports)

        def benchmark_data():
            index = json.loads(client.raw('forge-benchmarks', 'benchmark-data', 'latest.json', immutable=False))
            revision = index['data_commit'] if 'data_commit' in index else index['source_revisions']['forge-benchmarks']
            # Latest contains complete measured report to avoid mixed branch reads and public artifact auth.
            report = validate_benchmark(index)
            body = encoded(report)
            files['benchmarks/latest.json'] = body
            result['latest_benchmark'] = {'status': 'available', 'measured_at': report['measured_at'],
                'repository': 'forge-benchmarks', 'commit': revision, 'run_url': report.get('run_url', ''),
                'source_revisions': report['source_revisions'], 'summary': report['summary']}
        attempt('benchmarks', benchmark_data)
        client.save()
    # Local reviewed reports remain visible when absent from canonical source repos.
    # Reconcile them even when remote commits are unchanged after a site content edit.
    for group in ['documents', 'reports']:
        paths = {item['path']: item for item in result[group]}
        for item in groups[group]:
            present = paths.get(item['path'])
            if present is None or present.get('commit') is None:
                paths[item['path']] = item
                files[item['path']] = local_files[item['path']]
        result[group] = list(paths.values())
    for item in result['reports']:
        if item.get('commit') and item.get('repository'):
            source = item.get('source_path') or 'docs/' + PurePosixPath(item['path']).name
            alias = 'materials/' + ORG + '/' + item['repository'] + '/' + safe_path(source)
            files[alias] = files[item['path']]
    result['schema_version'] = 1
    result['errors'] = errors
    if sum(map(len, files.values())) > MAX_TOTAL:
        raise ValueError('Public snapshot exceeds aggregate size limit')
    # Timestamp changes alone never force file downloads or a fresh snapshot tree.
    stable = json.loads(json.dumps(result))
    for group in ['documents', 'reports']:
        for item in stable[group]:
            item.pop('url', None)
    stable['latest_benchmark'].pop('url', None)
    if stable.get('activity'):
        stable['activity'] = {key: value for key, value in stable['activity'].items() if key != 'updated_at'}
    identity = hashlib.sha256(encoded(stable)).hexdigest()[:24]
    if identity == old.get('content_id'):
        public_snapshot_modes(output / 'snapshots' / identity)
        if old.get('activity'):
            atomic_file(output / 'activity.json', encoded(old['activity']))
        atomic_file(output / 'refresh-status.json', encoded({'checked_at': now(), 'content_id': identity, 'errors': errors}))
        compatibility_materials(output, old)
        prune_snapshots(output, identity)
        return old
    result['generated_at'] = now()
    result['content_id'] = identity
    for group in ['documents', 'reports']:
        for item in result[group]:
            item['url'] = '/live/snapshots/' + identity + '/' + item['path']
    if result['latest_benchmark']['status'] == 'available':
        result['latest_benchmark']['url'] = '/live/snapshots/' + identity + '/benchmarks/latest.json'
    snapshots = output / 'snapshots'
    snapshots.mkdir(exist_ok=True)
    snapshots.chmod(0o755)
    destination = snapshots / identity
    if not destination.exists():
        staging = Path(tempfile.mkdtemp(prefix='.snapshot-', dir=output))
        try:
            for path, body in files.items():
                target = staging / safe_path(path)
                target.parent.mkdir(parents=True, exist_ok=True)
                directory = target.parent
                while directory != staging:
                    directory.chmod(0o755)
                    directory = directory.parent
                target.write_bytes(body)
                target.chmod(0o644)
            (staging / 'manifest.json').write_bytes(encoded(result))
            (staging / 'manifest.json').chmod(0o644)
            staging.chmod(0o755)
            os.replace(staging, destination)
        finally:
            if staging.exists():
                shutil.rmtree(staging)
    else:
        public_snapshot_modes(destination)
        result = json.loads((destination / 'manifest.json').read_text())
    temporary_link = output / '.current-new'
    if temporary_link.is_symlink():
        temporary_link.unlink()
    temporary_link.symlink_to('snapshots/' + identity)
    os.replace(temporary_link, output / 'current')
    if result['activity']:
        atomic_file(output / 'activity.json', encoded(result['activity']))
    atomic_file(output / 'refresh-status.json', encoded({'checked_at': now(), 'content_id': identity, 'errors': errors}))
    compatibility_materials(output, result)
    prune_snapshots(output, identity)
    return result





def compatibility_materials(output, manifest):
    """Keep legacy viewer endpoints tied to the same validated current tree."""
    output = Path(output)
    link = output / 'materials'
    temporary = output / '.materials-new'
    if temporary.is_symlink():
        temporary.unlink()
    temporary.symlink_to('current/materials')
    if link.exists() and not link.is_symlink():
        # Preserve a pre-existing generated tree as a reviewable backup, never erase it.
        backup = output / '.materials-legacy-backup'
        if backup.exists():
            raise ValueError('Legacy material backup already exists')
        os.replace(link, backup)
    os.replace(temporary, link)
    reports = []
    for item in manifest['reports']:
        remote = bool(item.get('commit') and item.get('repository'))
        repository = ORG + '/' + item['repository'] if remote else None
        source_path = item.get('source_path') or 'docs/' + PurePosixPath(item['path']).name
        file = '/materials/' + repository + '/' + source_path if remote else item['url']
        reports.append({'id': (repository + '/' + source_path) if remote else item['path'],
            'title': item['title'], 'path': source_path if remote else item['path'], 'file': file,
            'format': PurePosixPath(item['path']).suffix.lstrip('.'),
            'source': 'https://github.com/' + repository + '/blob/' + item['commit'] + '/' + source_path if remote else None,
            'repository': repository, 'kind': 'repository' if remote else 'local_snapshot',
            'sha256': item['sha256']})
    atomic_file(output / 'benchmarks.json', encoded({'updated_at': manifest['generated_at'], 'checked_at': now(),
        'poll_interval_seconds': 60, 'reports': reports,
        'errors': [error['component'] for error in manifest.get('errors', [])]}))


def public_snapshot_modes(root):
    """Also repair readable modes on trees generated by an older refresher."""
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise ValueError('Invalid public snapshot root')
    for directory, children, files in os.walk(root, followlinks=False):
        Path(directory).chmod(0o755)
        for name in files:
            path = Path(directory) / name
            if not path.is_symlink():
                path.chmod(0o644)


def prune_snapshots(output, current_id):
    """Bound disk use; preserve the current tree even when its data is older."""
    snapshots = Path(output) / 'snapshots'
    if not snapshots.is_dir():
        return
    directories = sorted([path for path in snapshots.iterdir()
                          if path.is_dir() and not path.is_symlink() and re.fullmatch(r'[0-9a-f]{24}', path.name)],
                         key=lambda path: path.stat().st_mtime, reverse=True)
    cutoff = time.time() - SNAPSHOT_MAX_AGE
    retained = 1  # Reserve a slot for current, regardless of its mtime/order.
    for path in directories:
        if path.name == current_id:
            continue
        if path.stat().st_mtime < cutoff or retained >= MAX_SNAPSHOTS:
            shutil.rmtree(path)
        else:
            retained += 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'public-data')
    parser.add_argument('--content', type=Path, default=ROOT / 'content')
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    import fcntl
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / '.refresh.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Public refresh already running')
            return
        client = None if args.offline else Client(args.output / '.refresh-cache.json', os.environ.get('GH_TOKEN'))
        result = refresh(args.output, args.content, client, args.offline)
        print('Public snapshot', result['content_id'], 'errors:', len(result['errors']))


if __name__ == '__main__':
    main()
