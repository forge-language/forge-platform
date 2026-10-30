import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHmac, randomUUID } from 'node:crypto';
import { mkdir, mkdtemp, readFile, writeFile, readdir } from 'node:fs/promises';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';
import { setTimeout } from 'node:timers/promises';
import { Client } from 'pg';

const root = fileURLToPath(new URL('../../', import.meta.url));
const cwd = fileURLToPath(new URL('../', import.meta.url));
const evidence = new URL('../../.omo/evidence/workers/', import.meta.url);
await mkdir(evidence, { recursive: true });
const run = (command, args, options = {}) => {
  const result = spawnSync(command, args, { encoding: 'utf8', ...options });
  if (result.status !== 0) throw new Error(`${command} failed: ${result.stderr || result.stdout}`);
  return result.stdout.trim();
};
const name = `forge-workers-test-${randomUUID()}`;
const secret = 'workers-isolated-integration-key';
const runtime = {};
let output = '';
try {
  run(process.execPath, ['scripts/build.mjs'], { cwd });
  run('docker', ['run', '-d', '--name', name, '-e', 'POSTGRES_PASSWORD=worker-test-only', '-e', 'POSTGRES_DB=forge_worker_test', '-p', '127.0.0.1::5432', 'postgres:16-alpine']);
  runtime.container = true;
  const mapping = run('docker', ['port', name, '5432/tcp']);
  const port = mapping.slice(mapping.lastIndexOf(':') + 1);
  const connectionString = `postgres://postgres:worker-test-only@127.0.0.1:${port}/forge_worker_test`;
  let ready = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    const status = spawnSync('docker', ['exec', name, 'pg_isready', '-h', '127.0.0.1', '-U', 'postgres', '-d', 'forge_worker_test']);
    if (status.status === 0) { ready = true; break; }
    await setTimeout(100);
  }
  assert.ok(ready, 'isolated database becomes ready');
  const client = new Client({ connectionString });
  await client.connect();
  for (const migration of (await readdir(`${root}backend/migrations`)).filter(name => name.endsWith('.sql')).sort()) {
    await client.query(await readFile(`${root}backend/migrations/${migration}`, 'utf8'));
  }
  await client.end();
  const socket = createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const address = socket.address();
  assert.ok(address && typeof address !== 'string');
  const workerPort = address.port;
  await new Promise((resolve, reject) => socket.close(error => error ? reject(error) : resolve()));
  const fixtureSocket = createServer();
  await new Promise(resolve => fixtureSocket.listen(0, '127.0.0.1', resolve));
  const fixtureAddress = fixtureSocket.address();
  assert.ok(fixtureAddress && typeof fixtureAddress !== 'string');
  const fixturePort = fixtureAddress.port;
  await new Promise((resolve, reject) => fixtureSocket.close(error => error ? reject(error) : resolve()));
  const fixture = spawn('python3', ['-c', 'import http.server,os,runpy; original=http.server.HTTPServer.__init__; http.server.HTTPServer.__init__=lambda self,address,*args,**kwargs:original(self,("127.0.0.1",int(os.environ["FIXTURE_PORT"])),*args,**kwargs); runpy.run_path(os.environ["FIXTURE_PATH"],run_name="__main__")'], { env: { ...process.env, FIXTURE_PORT: String(fixturePort), FIXTURE_PATH: `${root}tests/github-fixture.py`, REPOSITORY_CHECK_TOKEN: secret }, stdio: 'ignore' });
  runtime.fixture = fixture;
  let fixtureReady = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    try { await fetch(`http://127.0.0.1:${fixturePort}/health`); fixtureReady = true; break; }
    catch { await setTimeout(100); }
  }
  assert.ok(fixtureReady, 'repository fixture becomes ready');
  await mkdir(`${cwd}.wrangler`, { recursive: true });
  const directory = await mkdtemp(`${cwd}.wrangler/integration-`);
  const config = JSON.parse(await readFile(`${cwd}wrangler.jsonc`, 'utf8'));
  config.main = `${cwd}src/index.ts`;
  config.vars = { ...config.vars, JWT_SECRET: secret, ADMIN_GITHUB_ID: '1', REPOSITORY_CHECK_URL: `http://127.0.0.1:${fixturePort}`, REPOSITORY_CHECK_TOKEN: secret, CORS_ALLOWED_ORIGINS: 'https://forge.example', FRONTEND_URL: 'https://forge.example', BACKEND_BASE_URL: `http://127.0.0.1:${workerPort}` };
  config.hyperdrive[0].localConnectionString = connectionString;
  config.build.command = `node ${cwd}scripts/build.mjs`;
  await writeFile(`${directory}/wrangler.json`, JSON.stringify(config));
  const worker = spawn(process.execPath, ['node_modules/wrangler/bin/wrangler.js', 'dev', '--config', `${directory}/wrangler.json`, '--port', String(workerPort), '--ip', '127.0.0.1', '--local'], { cwd, env: { ...process.env, WRANGLER_SEND_METRICS: 'false' }, stdio: ['ignore', 'pipe', 'pipe'] });
  runtime.worker = worker;
  worker.stdout.on('data', chunk => { output += chunk.toString(); });
  worker.stderr.on('data', chunk => { output += chunk.toString(); });
  await new Promise((resolve, reject) => {
    const timer = globalThis.setTimeout(() => reject(new Error(`Worker startup timed out\n${output}`)), 60000);
    worker.on('exit', code => { globalThis.clearTimeout(timer); reject(new Error(`Worker exited ${code}\n${output}`)); });
    worker.stdout.on('data', () => { if (output.includes('Ready on')) { globalThis.clearTimeout(timer); resolve(); } });
  });
  const base = `http://127.0.0.1:${workerPort}`;
  const result = await new Promise((resolve, reject) => {
    const tests = spawn('python3', [`${root}tests/registry.py`], { env: { ...process.env, REGISTRY_BASE: base, TEST_JWT_SECRET: secret }, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = ''; let stderr = '';
    tests.stdout.on('data', chunk => { stdout += chunk.toString(); });
    tests.stderr.on('data', chunk => { stderr += chunk.toString(); });
    tests.on('error', reject);
    tests.on('close', status => resolve({ status, stdout, stderr }));
  });
  await writeFile(new URL('registry.log', evidence), `${result.stdout}${result.stderr}`);
  assert.equal(result.status, 0, `${result.stdout}${result.stderr}`);
  const token = user => {
    const header = Buffer.from(JSON.stringify({ alg: 'HS256', typ: 'JWT' })).toString('base64url');
    const payload = Buffer.from(JSON.stringify({ sub: user, github_id: user === 'RateTester' ? '999' : String(1000 + Number(user.replace('Parallel', ''))), role: 'user', exp: Math.floor(Date.now() / 1000) + 300 })).toString('base64url');
    return `${header}.${payload}.${createHmac('sha256', secret).update(`${header}.${payload}`).digest('base64url')}`;
  };
  await Promise.all(Array.from({ length: 32 }, async (_, index) => {
    const user = `Parallel${index}`;
    const response = await fetch(`${base}/api/me`, { headers: { Authorization: `Bearer ${token(user)}` } });
    assert.equal(response.status, 200);
    assert.equal((await response.json()).sub, user);
  }));
  const cors = await fetch(`${base}/api/packages`, { method: 'OPTIONS', headers: { Origin: 'https://forge.example' } });
  assert.equal(cors.status, 204);
  assert.equal(cors.headers.get('Access-Control-Allow-Origin'), 'https://forge.example');
  const untrusted = await fetch(`${base}/api/health`, { headers: { Origin: 'https://untrusted.example' } });
  assert.equal(untrusted.headers.get('Access-Control-Allow-Origin'), null);
  const limiterCodes = [];
  for (let index = 0; index < 31; index++) {
    const response = await fetch(`${base}/api/packages`, { method: 'POST', headers: { Authorization: `Bearer ${token('RateTester')}`, 'Content-Type': 'application/json' }, body: '{}' });
    limiterCodes.push(response.status);
  }
  assert.deepEqual(limiterCodes, [...Array(30).fill(400), 429]);
  const oversized = await fetch(`${base}/api/packages`, { method: 'POST', body: 'x'.repeat(2097153) });
  assert.equal(oversized.status, 413);
  run('docker', ['stop', '-t', '1', name]);
  for (let attempt = 0; attempt < 3; attempt++) {
    const unavailable = await fetch(`${base}/api/stats`);
    assert.equal(unavailable.status, 503);
  }
  await writeFile(new URL('surface.log', evidence), 'PASS: 32 simultaneous authenticated requests retain their own identity\nPASS: allowed/disallowed CORS and OPTIONS\nPASS: durable rate limiter permits 30 attempts and denies attempt 31\nPASS: body above 2 MiB returns 413\nPASS: three failed PostgreSQL connections return controlled 503 responses\n');
  await writeFile(new URL('worker.log', evidence), output);
  console.log('Worker integration passed; evidence: .omo/evidence/workers');
} finally {
  await writeFile(new URL('worker.log', evidence), output);
  runtime.worker?.kill('SIGTERM');
  runtime.fixture?.kill('SIGTERM');
  if (runtime.container) run('docker', ['rm', '-f', name]);
}
