#!/usr/bin/env python3
"""Read-only HTTP security checks; rate-limit probes require an isolated local fixture."""
import argparse
import concurrent.futures
import http.client
import os
import time
import unittest
from urllib.parse import urlsplit

parser = argparse.ArgumentParser()
parser.add_argument('--base', default=os.environ.get('REGISTRY_BASE', 'http://127.0.0.1:18103'))
parser.add_argument('--rate-tests', action='store_true', help='Consume rate-limit budgets on an isolated local fixture')
config, remaining = parser.parse_known_args()
base = urlsplit(config.base)
if config.rate_tests and (base.hostname not in ('127.0.0.1', 'localhost') or base.port in (None, 80, 443, 18101)):
    parser.error('Rate probes require an isolated localhost fixture port; production port 18101 is excluded')


def request(path, method='GET', headers=None, body=None):
    connection_type = http.client.HTTPSConnection if base.scheme == 'https' else http.client.HTTPConnection
    connection = connection_type(base.hostname, base.port, timeout=20)
    try:
        connection.request(method, path, body, headers or {})
        response = connection.getresponse()
        return response.status, {key.lower(): value for key, value in response.getheaders()}, response.read()
    finally:
        connection.close()


class SecurityTests(unittest.TestCase):
    def test_document_policy_and_no_sniff(self):
        status, headers, _ = request('/')
        self.assertEqual(status, 200)
        self.assertEqual(headers.get('x-content-type-options'), 'nosniff')
        self.assertIn(headers.get('referrer-policy'), ('same-origin', 'strict-origin-when-cross-origin'))
        policy = headers.get('content-security-policy', '')
        self.assertIn("object-src 'none'", policy)
        self.assertIn("frame-ancestors 'none'", policy)
        self.assertIn("connect-src 'self'", policy)
        self.assertNotIn("'unsafe-inline'", policy)
        self.assertNotIn("'unsafe-eval'", policy)

    def test_static_json_and_materials_no_sniff(self):
        for path in ['/activity.json', '/benchmarks.json', '/materials/index.json']:
            with self.subTest(path=path):
                status, headers, _ = request(path)
                self.assertIn(status, (200, 404))
                self.assertEqual(headers.get('x-content-type-options'), 'nosniff')

    def test_playground_isolated_runner(self):
        status, headers, _ = request('/playground/runner.html')
        self.assertEqual(status, 200)
        policy = headers.get('content-security-policy', '')
        self.assertIn("connect-src 'none'", policy)
        self.assertIn('sandbox allow-scripts', policy)
        self.assertNotIn('allow-same-origin', policy)
        self.assertEqual(headers.get('x-content-type-options'), 'nosniff')

    def test_source_and_configuration_not_exposed(self):
        for path in ['/.env', '/.git/config', '/.deployment/storage.env']:
            with self.subTest(path=path):
                status, _, body = request(path)
                self.assertIn(status, (403, 404))
                self.assertNotIn(b'JWT_SECRET=', body)
                self.assertNotIn(b'STORAGE_TOKEN=', body)

    def test_cross_origin_credentials_not_enabled(self):
        _, headers, _ = request('/api/health', headers={'Origin': 'https://untrusted.example'})
        self.assertNotEqual(headers.get('access-control-allow-origin'), '*')
        self.assertNotEqual(headers.get('access-control-allow-origin'), 'https://untrusted.example')

    def test_api_oversized_body_rejected_before_backend(self):
        connection_type = http.client.HTTPSConnection if base.scheme == 'https' else http.client.HTTPConnection
        connection = connection_type(base.hostname, base.port, timeout=10)
        try:
            connection.putrequest('POST', '/api/packages')
            connection.putheader('Content-Length', str(2 * 1024 * 1024 + 1))
            connection.putheader('Content-Type', 'application/json')
            connection.endheaders()
            self.assertEqual(connection.getresponse().status, 413)
        finally:
            connection.close()

    @unittest.skipUnless(config.rate_tests, '--rate-tests opts into consuming isolated fixture rate budgets')
    def test_login_rate_limit_resists_spoofed_client_headers(self):
        # This intentionally changes both forwarding headers. Only the configured tunnel gateway is trusted.
        def probe(index):
            return request('/api/auth/github/login', headers={'CF-Connecting-IP': f'203.0.113.{index + 1}', 'X-Real-IP': f'198.51.100.{index + 1}', 'X-Forwarded-For': f'192.0.2.{index + 1}'})[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=12) as executor:
            statuses = list(executor.map(probe, range(12)))
        self.assertIn(429, statuses, f'Expected login rate-limit rejection, received {statuses}')
        self.assertNotIn(500, statuses)


    @unittest.skipUnless(config.rate_tests, '--rate-tests opts into consuming isolated fixture rate budgets')
    def test_z_public_request_rate_budget(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
            statuses = list(executor.map(lambda _: request('/api/health')[0], range(400)))
        try:
            self.assertIn(429, statuses, f'Expected public request limit rejection, received {set(statuses)}')
            self.assertTrue(all(status in (200, 429) for status in statuses))
        finally:
            # Let the isolated fixture recover before another test suite uses it.
            time.sleep(3)


if __name__ == '__main__':
    unittest.main(argv=[__file__, *remaining])
