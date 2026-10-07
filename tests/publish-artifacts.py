#!/usr/bin/env python3
"""Exercise selected uploads and bounded retries with disposable loopback Storage."""
import hashlib
import http.server
import importlib.util
import json
import pathlib
import tempfile
import threading
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('publisher', pathlib.Path(__file__).parents[1] / 'scripts/publish-artifacts.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PublishArtifacts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='forge-publish-artifacts-')
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.releases = self.root / 'releases'
        self.releases.mkdir()
        self.patch = mock.patch.object(publisher, 'ROOT', self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        for version in ('0.3.0-preview.4', '0.3.0-preview.6'):
            directory = self.releases / version
            directory.mkdir()
            (directory / f'forge-{version}-linux-x86_64.tar.gz').write_bytes(version.encode())
            (directory / f'forge-{version}-linux-x86_64.tar.gz.sha256').write_bytes(b'checksum')
        self.old_path = '/releases/0.3.0-preview.4/forge-0.3.0-preview.4-linux-x86_64.tar.gz'
        self.old_url = 'https://storage.forge-lang.org/v1/objects/' + 'a' * 64
        self.config = self.releases / 'artifacts.conf'
        self.config.write_text(f'map $uri $forge_release_url {{\n default "";\n "{self.old_path}" "{self.old_url}";\n}}\n')
        self.calls = []
        self.statuses = []
        owner = self

        class Storage(http.server.BaseHTTPRequestHandler):
            def do_PUT(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                owner.calls.append((self.path, body, self.headers.get('Authorization')))
                status = owner.statuses.pop(0) if owner.statuses else 201
                self.send_response(status)
                self.send_header('Retry-After', '100')
                self.end_headers()
                self.wfile.write(json.dumps({'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)}).encode())

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Storage)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.origin = f'http://127.0.0.1:{self.server.server_port}'

    def publish(self):
        return publisher.publish(self.origin, 'https://storage.forge-lang.org', 'fixture-token', '0.3.0-preview.6')

    def test_selected_version_preserves_config_and_json_routes(self):
        old = {'sha256': 'a' * 64, 'url': self.old_url, 'size': 123}
        (self.releases / 'artifacts.json').write_text(json.dumps({self.old_path: old}))
        self.assertEqual(self.publish(), 2)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[0][1], b'0.3.0-preview.6')
        self.assertTrue(all(call[2] == 'Bearer fixture-token' for call in self.calls))
        records = json.loads((self.releases / 'artifacts.json').read_text())
        self.assertEqual(records[self.old_path]['size'], 123)
        self.assertEqual(records[self.old_path]['url'], self.old_url)
        self.assertEqual(len(records), 3)
        self.assertIn(self.old_url, self.config.read_text())

    def test_config_only_routes_preserved_and_transient_errors_retried(self):
        self.statuses = [429, 503, 201]
        with mock.patch.object(publisher.time, 'sleep') as sleep:
            self.assertEqual(self.publish(), 2)
        self.assertEqual(len(self.calls), 4)
        self.assertEqual(sleep.call_args_list, [mock.call(10), mock.call(10)])
        self.assertEqual(json.loads((self.releases / 'artifacts.json').read_text())[self.old_path]['url'], self.old_url)

    def test_retry_limit_and_no_route_changes_on_failure(self):
        original = self.config.read_bytes()
        self.statuses = [503] * 5
        with mock.patch.object(publisher.time, 'sleep') as sleep:
            with self.assertRaisesRegex(RuntimeError, '503'):
                self.publish()
        self.assertEqual(len(self.calls), 4)
        self.assertEqual(sleep.call_count, 3)
        self.assertEqual(self.config.read_bytes(), original)
        self.assertFalse((self.releases / 'artifacts.json').exists())

    def test_invalid_existing_route_and_selector_rejected_before_upload(self):
        for selector in ('../preview.5', '/tmp', '1\n', '1' * 65, '1..2'):
            with self.assertRaises(Exception):
                publisher.version(selector)
        self.config.write_text('map $uri $forge_release_url {\n default "";\n "../bad" "https://storage.forge-lang.org/v1/objects/' + 'a' * 64 + '";\n}\n')
        with self.assertRaises(ValueError):
            self.publish()
        self.assertEqual(self.calls, [])

    def test_retry_after_http_date_and_invalid_fallback_are_bounded(self):
        self.assertEqual(publisher.retry_delay('Fri, 01 Jan 2100 00:00:00 GMT', 0), 10)
        self.assertEqual(publisher.retry_delay('-5', 0), 0)
        self.assertEqual(publisher.retry_delay('invalid', 2), 4)


if __name__ == '__main__':
    unittest.main()
