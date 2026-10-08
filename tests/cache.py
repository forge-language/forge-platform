#!/usr/bin/env python3
"""Cache behavior checks against a disposable registry and its private Redis.

REGISTRY_BASE and CACHE_REDIS_CONTAINER must target the test compose project.
Redis cache contents are inspected directly; no production stack is modified.
"""
import json
import os
import subprocess
import time
import unittest

from registry import request, token, ROOT

CONTAINER = os.environ.get('CACHE_REDIS_CONTAINER', '')


def redis(*args):
    return subprocess.check_output(
        ['docker', 'exec', CONTAINER, 'redis-cli', '--raw', *args],
        text=True, timeout=10).rstrip('\n')


class CacheTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CONTAINER:
            raise unittest.SkipTest('CACHE_REDIS_CONTAINER is required')
        labels = json.loads(subprocess.check_output(
            ['docker', 'inspect', '-f', '{{json .Config.Labels}}', CONTAINER], text=True))
        if not labels.get('com.docker.compose.project', '').startswith('forge-registry-check-'):
            raise RuntimeError('Cache tests require an isolated forge-registry-check compose project')

    def current_keys(self):
        generation = redis('GET', 'forge:registry:generation:v1')
        return redis('KEYS', 'forge:registry:v1:' + generation + ':*').splitlines()

    def test_public_cache_has_bounded_ttl(self):
        status, value = request('GET', '/api/stats')
        self.assertEqual(status, 200)
        generation = redis('GET', 'forge:registry:generation:v1')
        prefix = 'forge:registry:v1:' + generation + ':'
        key = next(key for key in self.current_keys() if json.loads(key[len(prefix):]) == ['/api/stats'])
        self.assertEqual(json.loads(redis('GET', key)), value)
        self.assertGreater(int(redis('TTL', key)), 0)
        self.assertLessEqual(int(redis('TTL', key)), 30)

    def test_search_keys_and_auth_are_separate(self):
        for path in ['/api/packages?q=postgres', '/api/packages?q=web']:
            self.assertEqual(request('GET', path)[0], 200)
        keys = self.current_keys()
        self.assertTrue(any('postgres' in key for key in keys))
        self.assertTrue(any('web' in key for key in keys))
        self.assertEqual(request('GET', '/api/me')[0], 401)
        self.assertEqual(request('GET', '/api/me', auth=token())[0], 200)
        self.assertEqual(request('GET', '/api/packages/cache-never-published')[0], 404)
        self.assertEqual(set(keys), set(self.current_keys()))

    def test_publish_rotates_generation_and_cannot_repopulate_current_namespace(self):
        request('GET', '/api/packages?q=cache-test')
        old_generation = redis('GET', 'forge:registry:generation:v1')
        prefix = 'forge:registry:v1:' + old_generation + ':'
        old_key = next(key for key in self.current_keys() if json.loads(key[len(prefix):]) == ['/api/packages', 'cache-test'])
        value = json.loads((ROOT / 'backend/seed.json').read_text())[0]
        name = 'cache-test-' + str(time.time_ns())
        value.update(name=name, version='0.1.0',git_commit='a'*40,repository_url='https://github.com/TestUser/'+name+'__0_1_0',acknowledge_review=True)
        self.assertEqual(request('POST', '/api/packages', value, token('TestUser','user'))[0], 201)
        generation = redis('GET', 'forge:registry:generation:v1')
        self.assertNotEqual(old_generation, generation)
        # Model a database read completing after publish with its captured old key.
        redis('SET', old_key, '[]', 'EX', '30')
        status, body = request('GET', '/api/packages?q=cache-test')
        self.assertEqual(status, 200)
        self.assertIn(name, [row['name'] for row in body])

    def test_redis_outage_falls_back_to_postgres(self):
        subprocess.check_call(['docker', 'pause', CONTAINER], stdout=subprocess.DEVNULL)
        try:
            started = time.monotonic()
            self.assertEqual(request('GET', '/api/stats')[0], 200)
            self.assertLess(time.monotonic() - started, 3)
        finally:
            subprocess.check_call(['docker', 'unpause', CONTAINER], stdout=subprocess.DEVNULL)
            # Docker marks a paused container unhealthy; wait for its next probe
            # so dependent Compose services can be started after this test.
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                status = subprocess.check_output(['docker', 'inspect', '-f', '{{.State.Health.Status}}', CONTAINER], text=True).strip()
                if status == 'healthy':
                    break
                time.sleep(.2)
            else:
                self.fail('Disposable Redis did not recover its health check after unpause')


if __name__ == '__main__':
    unittest.main()
