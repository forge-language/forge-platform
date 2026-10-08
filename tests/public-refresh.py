#!/usr/bin/env python3
"""Mocked source refresh, provenance, filesystem and rollback regressions."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('refresh', ROOT / 'scripts/refresh-public-data.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
A, B = 'a' * 40, 'b' * 40


class FakeClient:
    def __init__(self):
        self.revision = A
        self.fail_document = False
        self.malformed_repositories = False
        self.downloads = []
        self.benchmark = None

    def api(self, path, **kwargs):
        if path.startswith('/orgs/'):
            if self.malformed_repositories:
                return {'bad': True}
            return [{'name': name, 'owner': {'login': 'forge-language'}, 'private': False,
                     'html_url': 'https://github.com/forge-language/' + name,
                     'description': name, 'default_branch': 'main', 'pushed_at': self.revision,
                     'stargazers_count': 1, 'forks_count': 0, 'open_issues_count': 0,
                     'archived': False} for name in ['forge', 'forge-benchmarks']]
        if '/pulls?' in path:
            return []
        if '/commits/' in path:
            return {'sha': self.revision}
        if '/git/trees/' in path:
            names = ['README.md', 'ARCHITECTURE.md'] if '/forge/' in path else ['docs/report-2026-10-06.md', 'docs/report.json']
            return {'tree': [{'path': name, 'type': 'blob'} for name in names]}
        raise AssertionError(path)

    def raw(self, repository, revision, path, **kwargs):
        self.downloads.append((repository, revision, path))
        if path == 'latest.json':
            if self.benchmark is None:
                raise ValueError('No measured report')
            return json.dumps(self.benchmark).encode()
        if path == 'ARCHITECTURE.md' and self.fail_document:
            raise ValueError('Partial source')
        return b'{"raw":true}' if path.endswith('.json') else ('Evidence ' + revision + ' ' + path).encode()

    def save(self):
        pass


def valid_report():
    runs = {'scheduler': [], 'strings': [], 'parser': []}
    for repeat in range(1, 4):
        for case in ['dispatch_10000_1', 'dispatch_10000_4', 'idle_50ms']:
            runs['scheduler'].append({'case': case, 'repeat': repeat, 'wall_ms': 50,
                                     'process_cpu_ms': 1, 'completed': 10000, 'coroutines': 10000})
        runs['parser'].append({'repeat': repeat, 'declarations': 100000, 'iterations': 15, 'parse_seconds': .5})
        for operation, sizes, implementations in [('scan', [4096, 16384, 65536], ['legacy', 'view']),
                ('append', [4096, 16384], ['legacy', 'builder']),
                ('append_slice', [4096, 16384, 65536], ['substring', 'view']),
                ('match', [4096, 16384, 65536], ['substring', 'view'])]:
            for size in sizes:
                for implementation in implementations:
                    runs['strings'].append({'repeat': repeat, 'operation': operation, 'bytes': size,
                        'implementation': implementation, 'seconds': .001, 'checksum': size})
    names = ['forge', 'forge-runtime', 'forge-stdlib', 'forge-benchmarks']
    return {'schema_version': 1, 'repeats': 3, 'measured_at': '2026-10-06T00:00:00Z',
            'source_revisions': {name: A for name in names}, 'source_tree_clean': {name: True for name in names},
            'checks': {'scheduler_completed': True, 'string_checksums': True, 'parser_declarations': True},
            'summary': {}, 'runs': runs}


class RefreshTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.content = self.directory / 'content'
        (self.content / 'project').mkdir(parents=True)
        (self.content / 'reports').mkdir()
        (self.content / 'project/README.md').write_text('Reviewed fallback')
        (self.content / 'reports/historical.json').write_text('{"measured":true}')
        self.output = self.directory / 'public-data'
        self.client = FakeClient()

    def refresh(self):
        return r.refresh(self.output, self.content, self.client)

    def test_offline_is_stable_and_retains_reviewed_content(self):
        first = r.refresh(self.output, self.content, offline=True)
        second = r.refresh(self.output, self.content, offline=True)
        self.assertEqual(first['content_id'], second['content_id'])
        self.assertEqual(first['generated_at'], second['generated_at'])
        self.assertEqual(len(list((self.output / 'snapshots').iterdir())), 1)
        self.assertEqual((self.output / 'current/documents/README.md').read_text(), 'Reviewed fallback')

    def test_partial_documents_rollback_and_retry(self):
        first = self.refresh()
        old_body = (self.output / 'current/documents/README.md').read_bytes()
        self.client.revision, self.client.fail_document = B, True
        failed = self.refresh()
        self.assertEqual(failed['source_revisions']['forge'], A)
        self.assertIn('documents', [error['component'] for error in failed['errors']])
        self.assertEqual((self.output / 'current/documents/README.md').read_bytes(), old_body)
        self.client.fail_document = False
        retried = self.refresh()
        self.assertEqual(retried['source_revisions']['forge'], B)
        self.assertIn(B, (self.output / 'current/documents/README.md').read_text())
        self.assertTrue((self.output / 'snapshots' / first['content_id']).exists())

    def test_unchanged_sources_do_not_redownload_content(self):
        first = self.refresh()
        self.client.downloads.clear()
        second = self.refresh()
        self.assertEqual(first['content_id'], second['content_id'])
        self.assertEqual([item[2] for item in self.client.downloads], ['latest.json'])

    def test_malformed_metadata_keeps_last_good(self):
        first = self.refresh()
        self.client.malformed_repositories = True
        second = self.refresh()
        self.assertEqual(first['repositories'], second['repositories'])
        self.assertIn('repositories', [error['component'] for error in second['errors']])

    def test_invalid_measurement_never_published(self):
        report = valid_report()
        del report['source_revisions']['forge']
        with self.assertRaises(ValueError):
            r.validate_benchmark(report)
        self.client.benchmark = valid_report()
        self.client.benchmark['checks']['string_checksums'] = False
        result = self.refresh()
        self.assertEqual(result['latest_benchmark']['status'], 'unavailable')
        self.assertFalse((self.output / 'current/benchmarks/latest.json').exists())

    def test_url_and_path_allowlist(self):
        client = r.Client(self.directory / 'cache.json')
        for url in ['http://api.github.com/orgs/forge-language/repos', 'https://example.com/x',
                    'https://raw.githubusercontent.com/other/forge/main/README.md',
                    'https://api.github.com/repos/forge-language/private-repo/commits/main',
                    'https://raw.githubusercontent.com/forge-language/forge/main/../secret']:
            with self.subTest(url=url), self.assertRaises(ValueError):
                client.get(url)
        for path in ['../secret', '/absolute', 'x/./y', 'x//y', 'x?token=y']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                r.safe_path(path)

    def test_public_permissions_under_restrictive_umask(self):
        self.client.benchmark = valid_report()
        mask = os.umask(0o077)
        try:
            self.refresh()
            r.Client(self.output / '.refresh-cache.json').save()
        finally:
            os.umask(mask)
        for path in [self.output, self.output / 'snapshots', self.output / 'current',
                     self.output / 'current/documents', self.output / 'current/reports', self.output / 'current/benchmarks']:
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o755, str(path))
        for path in [self.output / 'current/manifest.json', self.output / 'current/documents/README.md',
                     self.output / 'current/reports/report.json', self.output / 'current/benchmarks/latest.json',
                     self.output / 'activity.json', self.output / 'refresh-status.json']:
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o644, str(path))
        self.assertEqual(stat.S_IMODE((self.output / '.refresh-cache.json').stat().st_mode), 0o600)

    def test_existing_private_modes_repaired_without_snapshot_churn(self):
        first = self.refresh()
        for path, mode in [('current', 0o700), ('current/documents', 0o700),
                           ('current/manifest.json', 0o600), ('activity.json', 0o600)]:
            (self.output / path).chmod(mode)
        second = self.refresh()
        self.assertEqual(first['content_id'], second['content_id'])
        self.assertEqual(stat.S_IMODE((self.output / 'current').stat().st_mode), 0o755)
        self.assertEqual(stat.S_IMODE((self.output / 'current/manifest.json').stat().st_mode), 0o644)
        self.assertEqual(stat.S_IMODE((self.output / 'activity.json').stat().st_mode), 0o644)

    def test_unprivileged_nginx_uid_can_read_public_not_private_cache(self):
        if not shutil.which('docker') or subprocess.run(['docker', 'image', 'inspect', 'nginx:1.27-alpine'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            self.skipTest('Local nginx image unavailable; no pull')
        self.client.benchmark = valid_report()
        self.refresh()
        r.Client(self.output / '.refresh-cache.json').save()
        result = subprocess.run(['docker', 'run', '--rm', '--network', 'none', '--user', '101:101',
            '--volume', str(self.output) + ':/data:ro', '--entrypoint', 'sh', 'nginx:1.27-alpine', '-ec',
            'cat /data/current/manifest.json /data/current/documents/README.md /data/current/benchmarks/latest.json /data/activity.json /data/refresh-status.json >/dev/null; ! cat /data/.refresh-cache.json >/dev/null 2>&1'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_snapshot_retention_bounds_age_and_count_without_removing_current(self):
        snapshots = self.output / 'snapshots'
        snapshots.mkdir(parents=True)
        for number in range(70):
            path = snapshots / format(number, '024x')
            path.mkdir()
            os.utime(path, (time.time() - number * 60,) * 2)
        current, expired = 'f' * 24, 'e' * 24
        for identity in [current, expired]:
            path = snapshots / identity
            path.mkdir()
            os.utime(path, (time.time() - 8 * 86400,) * 2)
        r.prune_snapshots(self.output, current)
        self.assertEqual(len(list(snapshots.iterdir())), 64)
        self.assertTrue((snapshots / current).exists())
        self.assertFalse((snapshots / expired).exists())
        self.assertFalse((snapshots / format(69, '024x')).exists())

    def test_public_metadata_expands_but_content_allowlist_does_not(self):
        source = self.client.api('/orgs/forge-language/repos')
        for name in ['forge-storage', 'future-module']:
            item = copy.deepcopy(source[0])
            item['name'], item['html_url'] = name, 'https://github.com/forge-language/' + name
            source.append(item)
        private = copy.deepcopy(source[0])
        private['name'], private['private'] = 'private-module', True
        source.append(private)
        names = {item['name'] for item in r.metadata(source)}
        self.assertIn('forge-storage', names)
        self.assertIn('future-module', names)
        self.assertNotIn('private-module', names)
        with self.assertRaises(ValueError):
            r.Client(self.directory / 'cache.json').get('https://raw.githubusercontent.com/forge-language/future-module/main/README.md')

    def test_complete_string_workloads_required(self):
        good = valid_report()
        self.assertEqual(len(good['runs']['strings']), 66)
        r.validate_benchmark(good)
        mutations = [lambda rows: rows.pop(), lambda rows: rows.append(copy.deepcopy(rows[0])),
                     lambda rows: rows[0].update(bytes=1024), lambda rows: rows[0].update(checksum=1),
                     lambda rows: rows[0].update(seconds=float('nan'))]
        for mutate in mutations:
            report = copy.deepcopy(good)
            mutate(report['runs']['strings'])
            with self.assertRaises(ValueError):
                r.validate_benchmark(report)

    def test_legacy_catalog_uses_validated_immutable_materials(self):
        self.refresh()
        catalog = json.loads((self.output / 'benchmarks.json').read_text())
        remote = next(item for item in catalog['reports'] if item['repository'] == 'forge-language/forge-benchmarks')
        body = (self.output / remote['file'].lstrip('/')).read_bytes()
        self.assertEqual(hashlib.sha256(body).hexdigest(), remote['sha256'])
        self.assertIn('/blob/' + A + '/', remote['source'])
        self.assertEqual(os.readlink(self.output / 'materials'), 'current/materials')
        self.assertTrue(any(item['kind'] == 'local_snapshot' for item in catalog['reports']))

    def test_local_content_change_refreshes_unchanged_remote(self):
        first = self.refresh()
        (self.content / 'reports/new-evidence.md').write_text('Reviewed evidence')
        second = self.refresh()
        self.assertNotEqual(first['content_id'], second['content_id'])
        report = next(item for item in second['reports'] if item['path'] == 'reports/new-evidence.md')
        self.assertIsNone(report['commit'])
        self.assertIn('snapshot', report['provenance'])

    def test_changed_metadata_revalidates_heads_instead_of_freezing_stale_revision(self):
        class CachedHeadClient(FakeClient):
            head_requests = None

            def __init__(self):
                super().__init__()
                self.head_requests = []

            def api(self, path, **kwargs):
                if '/commits/' in path:
                    self.head_requests.append((path, kwargs.get('ttl', 300)))
                    return {'sha': self.revision if kwargs.get('ttl') == 0 else A}
                result = super().api(path, **kwargs)
                if path.startswith('/orgs/'):
                    for name in ['forge-platform', 'forge-storage']:
                        record = copy.deepcopy(result[0])
                        record.update(name=name, html_url='https://github.com/forge-language/' + name)
                        result.append(record)
                if any('/' + name + '/git/trees/' in path for name in ['forge-platform', 'forge-storage']):
                    return {'tree': [{'type': 'blob', 'path': 'docs/performance.md'}]}
                return result
        self.client = CachedHeadClient()
        first = self.refresh()
        self.assertEqual(first['source_revisions']['forge'], A)
        self.client.head_requests.clear()
        self.client.revision = B
        second = self.refresh()
        self.assertEqual(second['source_revisions'], {'forge': B, 'forge-benchmarks': B})
        for name in ['forge', 'forge-platform', 'forge-storage']:
            self.assertEqual(second['report_source_revisions'][name], B)
        requests = self.client.head_requests
        self.assertEqual(len(requests), 4)
        self.assertTrue(all(ttl == 0 for _, ttl in requests), requests)
        self.assertEqual({path.split('/')[3] for path, _ in requests},
                         {'forge', 'forge-benchmarks', 'forge-platform', 'forge-storage'})
        self.client.head_requests.clear()
        third = self.refresh()
        self.assertEqual(third['source_revisions'], second['source_revisions'])
        self.assertEqual(self.client.head_requests, [])

    def test_platform_report_inventory_accepts_published_size_and_rejects_overflow(self):
        class PlatformClient(FakeClient):
            count = 37

            def api(self, path, **kwargs):
                result = super().api(path, **kwargs)
                if path.startswith('/orgs/'):
                    platform = copy.deepcopy(result[0])
                    platform.update(name='forge-platform', html_url='https://github.com/forge-language/forge-platform')
                    result.append(platform)
                if '/forge-platform/git/trees/' in path:
                    result = {'tree': [{'type': 'blob', 'path': f'docs/performance-{number}.md'}
                                       for number in range(self.count)]}
                return result
        self.client = PlatformClient()
        first = self.refresh()
        reports = [item for item in first['reports'] if item['repository'] == 'forge-platform']
        self.assertEqual(len(reports), 37)
        self.assertNotIn('reports:forge-platform', [item['component'] for item in first['errors']])
        self.client.count, self.client.revision = 65, B
        second = self.refresh()
        self.assertIn('reports:forge-platform', [item['component'] for item in second['errors']])
        self.assertEqual(second['report_source_revisions']['forge-platform'], A)
        self.assertEqual({item['path']: (item['commit'], item['sha256']) for item in second['reports'] if item['repository'] == 'forge-platform'},
                         {item['path']: (item['commit'], item['sha256']) for item in reports})

    def test_other_repository_reports_survive_benchmark_source_refresh(self):
        class ExtraClient(FakeClient):
            benchmark_revision = A

            def api(self, path, **kwargs):
                if '/commits/' in path:
                    return {'sha': self.benchmark_revision if '/forge-benchmarks/' in path else A}
                result = super().api(path, **kwargs)
                if path.startswith('/orgs/'):
                    for item in result:
                        item['pushed_at'] = self.benchmark_revision if item['name'] == 'forge-benchmarks' else A
                if '/forge/git/trees/' in path:
                    result['tree'].append({'path': 'docs/extra-performance.md', 'type': 'blob'})
                return result
        self.client = ExtraClient()
        self.refresh()
        path = 'reports/forge/docs/extra-performance.md'
        body = (self.output / 'current' / path).read_bytes()
        self.client.benchmark_revision = B
        second = self.refresh()
        self.assertEqual(second['source_revisions']['forge-benchmarks'], B)
        self.assertTrue(any(item['path'] == path for item in second['reports']))
        self.assertEqual((self.output / 'current' / path).read_bytes(), body)
        self.assertEqual((self.output / 'materials/forge-language/forge/docs/extra-performance.md').read_bytes(), body)


if __name__ == '__main__':
    unittest.main()
