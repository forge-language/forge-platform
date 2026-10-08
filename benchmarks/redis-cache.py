#!/usr/bin/env python3
"""Compare Redis warm-cache reads with PostgreSQL reads on an isolated stack.

The API port excludes Nginx/public edge rate limiting. Each request opens a new
HTTP connection, so this measures local end-to-end latency, not engine capacity.
The fixture is always removed, including its database volume, on exit.
"""
import argparse
import concurrent.futures
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DATE = '2026-10-06'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--samples', type=int, default=100)
    parser.add_argument('--api-port', type=int, default=18108)
    parser.add_argument('--site-port', type=int, default=18107)
    parser.add_argument('--image', default='forge-platform-backend:storage-test')
    parser.add_argument('--verify', action='store_true', help='Also run registry, cache and isolated security checks')
    config = parser.parse_args()
    if config.samples < 20 or config.api_port in (18101, 18103) or config.site_port in (18101, 18103):
        parser.error('Use at least 20 samples and independent fixture ports')
    project = 'forge-registry-check-cache-' + str(os.getpid())
    results = []
    base = 'http://127.0.0.1:' + str(config.api_port)
    with tempfile.TemporaryDirectory(prefix='forge-redis-benchmark-') as temporary:
        directory = Path(temporary)
        envfile = directory / 'config.env'
        envfile.write_text((ROOT / 'tests/config.env').read_text() + '\n' + '\n'.join([
            'SITE_PORT=' + str(config.site_port),
            'FRONTEND_URL=http://localhost:' + str(config.site_port),
            'BACKEND_BASE_URL=http://localhost:' + str(config.site_port),
            'FORGE_PROXY_SUBNET=172.28.244.0/29',
            'FORGE_PROXY_IP=172.28.244.3',
            'STORAGE_BASE_URL=', 'STORAGE_TOKEN=',
        ]) + '\n')
        override = directory / 'override.yml'
        prefix = ['docker', 'compose', '--env-file', str(envfile), '-p', project,
                  '-f', str(ROOT / 'compose.yml'), '-f', str(ROOT / 'tests/compose.yml'), '-f', str(override)]

        def compose(*args, capture=False):
            return subprocess.run(prefix + list(args), cwd=ROOT, check=True,
                                  text=True, capture_output=capture)

        def configure(enabled):
            override.write_text('services:\n api:\n  image: ' + config.image + '\n  environment:\n   REDIS_HOST: "' + ('redis' if enabled else '') +
                                '"\n   STORAGE_BASE_URL: ""\n   STORAGE_TOKEN: ""\n  ports: ["127.0.0.1:' + str(config.api_port) + ':8080"]\n')

        def request():
            tick = time.perf_counter()
            with urllib.request.urlopen(base + '/api/packages', timeout=10) as response:
                if response.status != 200:
                    raise RuntimeError('Benchmark request failed: ' + str(response.status))
                body = response.read()
            return (time.perf_counter() - tick) * 1000, body

        def redis_hits():
            output = compose('exec', '-T', 'redis', 'redis-cli', 'INFO', 'stats', capture=True).stdout
            return int(next(line.split(':', 1)[1] for line in output.splitlines() if line.startswith('keyspace_hits:')))

        configure(False)
        try:
            compose('up', '-d', '--no-build', '--wait', 'api')
            testenv = dict(os.environ, REGISTRY_BASE=base, TEST_JWT_SECRET='registry-integration-only')
            subprocess.run(['python3', str(ROOT / 'tests/registry.py'), '--seed'], env=testenv, check=True)
            api_id = compose('ps', '-q', 'api', capture=True).stdout.strip()
            measured_image_id = subprocess.check_output(['docker', 'inspect', '-f', '{{.Image}}', api_id], text=True).strip()
            expected = json.loads(request()[1])
            package_count = len(expected)
            for enabled in [False, True]:
                if enabled:
                    configure(True)
                    compose('up', '-d', '--no-build', '--wait', '--force-recreate', 'api')
                    api_id = compose('ps', '-q', 'api', capture=True).stdout.strip()
                    if subprocess.check_output(['docker', 'inspect', '-f', '{{.Image}}', api_id], text=True).strip() != measured_image_id:
                        raise RuntimeError('The API image tag changed between variants; retry with an immutable image')
                for _ in range(5):
                    if json.loads(request()[1]) != expected:
                        raise RuntimeError('Cached and uncached payloads differ')
                for concurrency in [1, 8]:
                    hits_before = redis_hits()
                    tick = time.perf_counter()
                    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
                        measured = list(executor.map(lambda _: request(), range(config.samples)))
                    elapsed = time.perf_counter() - tick
                    hits_after = redis_hits()
                    samples = [sample for sample, body in measured]
                    if any(json.loads(body) != expected for _, body in measured):
                        raise RuntimeError('Measured payloads differ')
                    ordered = sorted(samples)
                    results.append({
                        'variant': 'redis-warm' if enabled else 'postgres-only',
                        'endpoint': '/api/packages', 'concurrency': concurrency,
                        'samples': config.samples, 'errors': 0,
                        'median_ms': round(statistics.median(samples), 4),
                        'p95_ms': round(ordered[math.ceil(len(ordered) * .95) - 1], 4),
                        'mean_ms': round(statistics.mean(samples), 4),
                        'min_ms': round(min(samples), 4), 'max_ms': round(max(samples), 4),
                        'elapsed_seconds': round(elapsed, 4),
                        'redis_keyspace_hits_delta': hits_after - hits_before,
                        'response_bytes': len(measured[0][1]),
                        'latencies_ms': [round(value, 4) for value in samples],
                    })
            if config.verify:
                redis_id = compose('ps', '-q', 'redis', capture=True).stdout.strip()
                testenv['CACHE_REDIS_CONTAINER'] = redis_id
                subprocess.run(['python3', str(ROOT / 'tests/registry.py')], env=testenv, check=True)
                subprocess.run(['python3', str(ROOT / 'tests/cache.py')], env=testenv, check=True)
                compose('up', '-d', '--no-build', '--wait', 'site')
                subprocess.run(['python3', str(ROOT / 'tests/security.py'), '--base',
                                'http://127.0.0.1:' + str(config.site_port), '--rate-tests'], check=True)
                # A warm GET must survive database outage; a missing entry must not.
                expected = json.loads(request()[1])
                compose('stop', 'postgres')
                if json.loads(request()[1]) != expected:
                    raise RuntimeError('Cache hit while database was offline changed payload')
            report = {
                'recorded_at_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                'environment': {'host_os': platform.platform(), 'host_arch': platform.machine(),
                                'backend_image': config.image,
                                'backend_image_id': measured_image_id,
                                'postgres_image': 'postgres:16-alpine', 'redis_image': 'redis:7.4-alpine'},
                'method': {'endpoint': '/api/packages', 'packages': package_count, 'warmup_requests': 5,
                           'samples_per_variant_and_concurrency': config.samples,
                           'concurrency': [1, 8], 'http_connection_reuse': False,
                           'baseline': 'Same API image with REDIS_HOST empty; PostgreSQL read for each request',
                           'cache': 'Same API image with Redis enabled; prewarmed public GET entry',
                           'scope': 'Local loopback HTTP directly to API; excludes Nginx, TLS and Cloudflare',
                           'limitations': 'Small seeded catalog, one host and one ordered run; latency includes fresh connections and Python scheduling. Not a general throughput or production speed claim.'},
                'results': results,
            }
            target = ROOT / 'benchmarks' / ('redis-cache-' + DATE + '.json')
            target.write_text(json.dumps(report, indent=2) + '\n')
            write_document(report)
            print('Wrote ' + str(target))
        finally:
            compose('down', '-v', '--remove-orphans')


def write_document(report):
    rows = ['| Variant | Concurrent clients | Samples | Median (ms) | p95 (ms) | Redis hits |',
            '| --- | ---: | ---: | ---: | ---: | ---: |']
    for row in report['results']:
        rows.append('| {variant} | {concurrency} | {samples} | {median_ms:.4f} | {p95_ms:.4f} | {redis_keyspace_hits_delta} |'.format(**row))
    text = '# Redis cache measurement — ' + DATE + '\n\n'
    text += 'Recorded at `' + report['recorded_at_utc'] + '` on an isolated local Compose fixture. The same Forge API image was tested first with `REDIS_HOST` empty, then with a warmed Redis cache.\n\n'
    text += '\n'.join(rows) + '\n\n'
    text += 'Each variant has five initial warmup requests and ' + str(report['method']['samples_per_variant_and_concurrency']) + ' measured requests at each concurrency level. The public `/api/packages` response contains ' + str(report['method']['packages']) + ' seeded packages. The client opens a fresh HTTP connection for every request. Redis hit counters confirm actual cache access; two hits per request correspond to generation lookup and payload lookup. All measured payloads matched the PostgreSQL baseline.\n\n'
    text += 'These are local latency measurements, including HTTP connection setup and Python scheduling. They exclude Nginx, TLS and Cloudflare. This small catalog and one ordered run do not establish production throughput or a universal speedup; Redis avoids repeated database reads but adds connection and protocol overhead.\n\n'
    text += 'Cache policy: public registry GETs only, 30-second TTL, 256 KiB maximum body, UUID generation rotation after successful publish. Late fills remain in the previous namespace. Redis failures fall back to PostgreSQL; if publish invalidation fails, existing entries can remain stale until their TTL expires.\n\n'
    text += 'Raw samples and image identity: [`benchmarks/redis-cache-' + DATE + '.json`](../benchmarks/redis-cache-' + DATE + '.json). Reproduce with:\n\n```sh\npython3 benchmarks/redis-cache.py --verify\n```\n'
    (ROOT / 'docs' / ('redis-cache-performance-' + DATE + '.md')).write_text(text)


if __name__ == '__main__':
    main()
