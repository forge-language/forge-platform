#!/usr/bin/env python3
"""Benchmark only labeled disposable registry APIs; requires Python aiohttp."""
import asyncio, base64, hashlib, hmac, json, os
from pathlib import Path
import platform, statistics, subprocess, time
import aiohttp
ROOT = Path(__file__).resolve().parents[1]
LABEL = '20261002-security'
BEFORE = os.environ.get('REGISTRY_BEFORE_IMAGE', 'forge-platform-security-before:20261002')
AFTER = os.environ.get('REGISTRY_AFTER_IMAGE', 'forge-platform-backend:local')
REPEATS, SECONDS, WARMUP = 3, 4, 1
CLIENTS = [1, 16, 64]
PATHS = ['/api/health', '/api/packages', '/api/packages/forge-web']
SERVERS = {name: ('http://127.0.0.1:' + str(port), 'forge-registry-perf-' + name)
           for name, port in [('before', 18231), ('after', 18232)]}
CREATED = []
NETWORK = 'forge-registry-perf-20261002'
network_created = False

def docker(*args):
    return subprocess.check_output(['docker', *args], text=True).strip()

def resources(container):
    pid = docker('inspect', '-f', '{{.State.Pid}}', container)
    relative = Path('/proc/' + pid + '/cgroup').read_text().split('0::', 1)[1].strip()
    return Path('/sys/fs/cgroup') / relative.lstrip('/')

def usage(group):
    cpu = dict(line.split() for line in (group / 'cpu.stat').read_text().splitlines())
    return int(cpu['usage_usec']), int((group / 'memory.current').read_text())

def clean_payload(value):
    if isinstance(value, dict):
        return {k: clean_payload(v) for k, v in value.items() if k != 'published_at'}
    if isinstance(value, list):
        return [clean_payload(v) for v in value]
    return value

def jwt():
    def b64(v):
        return base64.urlsafe_b64encode(json.dumps(v).encode()).rstrip(b'=')
    signed = b64({'alg': 'HS256'}) + b'.' + b64({'sub': 'Helloworld0822', 'role': 'admin', 'exp': int(time.time()) + 3600})
    return (signed + b'.' + base64.urlsafe_b64encode(hmac.new(b'benchmark-only-secret', signed, hashlib.sha256).digest()).rstrip(b'=')).decode()

async def ready(session, url):
    for _ in range(60):
        try:
            async with session.get(url + '/api/health') as response:
                if response.status == 200:
                    return
        except aiohttp.ClientError:
            pass
        await asyncio.sleep(1)
    raise RuntimeError('Benchmark API failed to start')

async def trial(name, path, clients, seconds):
    url, container = SERVERS[name]
    group = resources(container)
    before, _ = usage(group)
    latencies, memory = [], []
    errors = 0
    start = time.monotonic()
    deadline = start + seconds
    async with aiohttp.ClientSession(connector=aiohttp.TCPConnector(limit=clients), timeout=aiohttp.ClientTimeout(total=10), auto_decompress=False) as session:
        async def worker():
            nonlocal errors
            while time.monotonic() < deadline:
                tick = time.monotonic()
                try:
                    async with session.get(url + path, headers={'Accept-Encoding': 'identity'}) as response:
                        if response.status != 200 or not await response.read():
                            errors += 1
                except aiohttp.ClientError:
                    errors += 1
                latencies.append((time.monotonic() - tick) * 1000)
        async def sample():
            while time.monotonic() < deadline:
                memory.append(usage(group)[1])
                await asyncio.sleep(.2)
        await asyncio.gather(*(worker() for _ in range(clients)), sample())
    elapsed = time.monotonic() - start
    after, _ = usage(group)
    ordered = sorted(latencies)
    return {'variant': name, 'endpoint': path, 'clients': clients, 'requests': len(latencies), 'errors': errors,
            'seconds': round(elapsed, 3), 'rps': round(len(latencies) / elapsed, 2),
            'p95_ms': round(ordered[max(0, int(len(ordered) * .95) - 1)], 3),
            'cpu_core_percent': round((after - before) / 1e6 / elapsed * 100, 2),
            'memory_mib': round(max(memory, default=0) / 1024**2, 2)}

async def main():
    global network_created
    # Refuse collisions, and clean up only resources created by this invocation.
    for name in [NETWORK, 'forge-registry-perf-db', *(v[1] for v in SERVERS.values())]:
        kind = 'network' if name == NETWORK else 'container'
        if subprocess.run(['docker', kind, 'inspect', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError('Existing benchmark resource: ' + name)
    image_ids = {name: docker('image', 'inspect', '-f', '{{.Id}}', image) for name, image in [('before', BEFORE), ('after', AFTER)]}
    expected = os.environ.get('REGISTRY_BEFORE_EXPECTED_ID', 'sha256:800b8e446e8d078513651e15f10ec7e5568a01362aee3d3e66fe0ba4c4329fb1')
    if image_ids['before'] != expected:
        raise RuntimeError('Frozen baseline image changed')
    docker('network', 'create', '--label', 'forge.registry.benchmark=' + LABEL, NETWORK)
    network_created = True
    db = 'forge-registry-perf-db'
    docker('run', '-d', '--name', db, '--label', 'forge.registry.benchmark=' + LABEL, '--network', NETWORK,
           '--tmpfs', '/var/lib/postgresql/data', '-e', 'POSTGRES_USER=bench', '-e', 'POSTGRES_PASSWORD=benchmark-only', '-e', 'POSTGRES_DB=before', 'postgres:16-alpine')
    CREATED.append(db)
    for _ in range(60):
        if subprocess.run(['docker', 'exec', db, 'pg_isready', '-U', 'bench'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            break
        await asyncio.sleep(1)
    else:
        raise RuntimeError('Benchmark PostgreSQL failed to start')
    docker('exec', db, 'createdb', '-U', 'bench', 'after')
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=10)) as session:
        for name, image in [('before', BEFORE), ('after', AFTER)]:
            url, container = SERVERS[name]
            docker('run', '-d', '--name', container, '--label', 'forge.registry.benchmark=' + LABEL,
                   '--network', NETWORK, '--cpus', '2', '--memory', '512m', '-p', '127.0.0.1:' + url.rsplit(':', 1)[1] + ':8080',
                   '-e', 'DATABASE_URL=postgres://bench:benchmark-only@' + db + '/' + name,
                   '-e', 'JWT_SECRET=benchmark-only-secret', image)
            CREATED.append(container)
            await ready(session, url)
            for value in json.loads((ROOT / 'backend/seed.json').read_text()):
                async with session.post(url + '/api/packages', json=value, headers={'Authorization': 'Bearer ' + jwt()}) as response:
                    if response.status != 201:
                        raise RuntimeError('Seed publication failed: ' + await response.text())
        for path in PATHS:
            values = []
            for url, container in SERVERS.values():
                if docker('inspect', '-f', '{{index .Config.Labels "forge.registry.benchmark"}}', container) != LABEL:
                    raise RuntimeError('Only disposable containers are permitted')
                async with session.get(url + path) as response:
                    assert response.status == 200
                    values.append(clean_payload(await response.json()))
            assert values[0] == values[1], 'Response mismatch: ' + path
    results = []
    for path in PATHS:
        for clients in CLIENTS:
            for repeat in range(REPEATS):
                for name in (['before', 'after'] if repeat % 2 == 0 else ['after', 'before']):
                    await trial(name, path, clients, WARMUP)
                    row = await trial(name, path, clients, SECONDS)
                    row['repeat'] = repeat + 1
                    results.append(row)
                    print(json.dumps(row), flush=True)
    source_hash = hashlib.sha256()
    sources = [*sorted((ROOT / 'backend/src').glob('*.fg')), ROOT / 'backend/native/adapter.c']
    for module, files in {'forge-postgres': ['CMakeLists.txt', 'postgres.fg', 'src/bridge.c'], 'forge-web': ['CMakeLists.txt', 'web.fg', 'src/bridge.c']}.items():
        sources += [ROOT / 'vendor' / module / file for file in files]
    for file in sorted(sources):
        source_hash.update(str(file.relative_to(ROOT)).encode() + b'\0' + file.read_bytes() + b'\0')
    report = {'date_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'host': platform.platform(),
              'image_ids': image_ids, 'after_source_sha256': source_hash.hexdigest(), 'before_revision': 'f2e301aaa732cdd67522f82c119db573868b7cb8',
              'after_revision': docker('image', 'inspect', '-f', '{{index .Config.Labels "forge.source"}}', AFTER) or 'working tree',
              'limits': {'cpu_cores': 2, 'memory_mib': 512, 'pool_connections': 5, 'http_workers': 8,
                         'repeats': REPEATS, 'trial_seconds': SECONDS, 'warmup_seconds': WARMUP,
                         'fixture': 'identical official registry manifests, separate databases in one disposable PostgreSQL16 instance',
                         'equivalence': 'all response fields except independently generated published_at timestamps'},
              'results': results}
    output = ROOT / 'docs/registry-security-performance.json'
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    lines = ['# Registry security and performance', '', f"Measured {report['date_utc']}; 3 alternating before/after repeats of 4 seconds after 1 second warmup. Values are medians.", '',
             '| API | Clients | Before req/s | After req/s | Change | Before p95 ms | After p95 ms | Errors |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for path in PATHS:
        for clients in CLIENTS:
            groups = {name: [r for r in results if r['variant'] == name and r['endpoint'] == path and r['clients'] == clients] for name in SERVERS}
            b, a = (statistics.median(r['rps'] for r in groups[name]) for name in SERVERS)
            bp, ap = (statistics.median(r['p95_ms'] for r in groups[name]) for name in SERVERS)
            errors = sum(r['errors'] for group in groups.values() for r in group)
            lines.append(f'| {path} | {clients} | {b:.2f} | {a:.2f} | {(a/b-1)*100:+.2f}% | {bp:.3f} | {ap:.3f} | {errors} |')
    lines += ['', '2 CPUs / 512 MiB per API, five PostgreSQL leases, eight HTTP workers. Image IDs, raw trials, CPU and memory are in JSON. Responses match except publish timestamps.', '',
              'The after variant removes database leasing from stateless routes and caches prepared SQL plans. Both variants read registry data and ownership from PostgreSQL on every request. The after variant also includes stricter JSON/JWT parsing and explicit proxy trust. This combined comparison does not isolate each change.', '',
              'This shared host still runs production applications. Small differences can be host variation. Python aiohttp may limit high-throughput endpoints. This is a registry application comparison, not a Rust/Forge language claim. Existing published SDK binaries and production deployments are not updated by this benchmark.']
    output.with_suffix('.md').write_text('\n'.join(lines) + '\n')
    if any(r['errors'] for r in results):
        raise RuntimeError('Request errors occurred')

if __name__ == '__main__':
    try:
        asyncio.run(main())
    finally:
        for container in reversed(CREATED):
            subprocess.run(['docker', 'rm', '-f', container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if network_created:
            subprocess.run(['docker', 'network', 'rm', NETWORK], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
