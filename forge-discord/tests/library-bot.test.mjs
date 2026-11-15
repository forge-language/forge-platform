import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createServer as createHttpsServer } from 'node:https';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { spawn, spawnSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import test from 'node:test';

test('Forge functions deliver commands and replies, report failures, and release transport handles', { timeout: 60000 }, async t => {
  const delivered = [];
  let forbidden = 0;
  let retried = false;
  const directory = await mkdtemp(join(tmpdir(), 'forge-bot-gateway-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const certificate = spawnSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', join(directory, 'key.pem'),
    '-out', join(directory, 'cert.pem'), '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost', '-days', '1'], { stdio: 'ignore' });
  assert.equal(certificate.status, 0);
  const sockets = new Set();
  const identified = [];
  const gateway = createHttpsServer({ key: await readFile(join(directory, 'key.pem')), cert: await readFile(join(directory, 'cert.pem')) });
  t.after(() => { for (const socket of sockets) socket.destroy(); return new Promise(resolveClosed => gateway.close(resolveClosed)); });
  const frame = value => {
    const payload = Buffer.from(JSON.stringify(value));
    const header = Buffer.alloc(payload.length < 126 ? 2 : 4);
    header[0] = 129;
    if (payload.length < 126) header[1] = payload.length;
    else { header[1] = 126; header.writeUInt16BE(payload.length, 2); }
    return Buffer.concat([header, payload]);
  };
  gateway.on('upgrade', (request, socket) => {
    sockets.add(socket); socket.once('close', () => sockets.delete(socket));
    const accept = createHash('sha1').update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    socket.write(frame({ op: 10, d: { heartbeat_interval: 10000 } }));
    let pending = Buffer.alloc(0);
    socket.on('data', bytes => {
      pending = Buffer.concat([pending, bytes]);
      while (pending.length >= 2) {
        const opcode = pending[0] & 15;
        assert.equal(pending[1] & 128, 128);
        let length = pending[1] & 127; let offset = 2;
        if (length === 126) { if (pending.length < 4) return; length = pending.readUInt16BE(2); offset = 4; }
        assert.notEqual(length, 127);
        if (pending.length < offset + 4 + length) return;
        const mask = pending.subarray(offset, offset + 4); offset += 4;
        const payload = Buffer.from(pending.subarray(offset, offset + length));
        for (let i = 0; i < payload.length; i += 1) payload[i] ^= mask[i % 4];
        pending = pending.subarray(offset + length);
        if (opcode === 8) { socket.end(); return; }
        if (opcode !== 1) continue;
        const value = JSON.parse(payload.toString());
        if (value.op === 1) socket.write(frame({ op: 11, d: null }));
        if (value.op === 2) {
          identified.push({ token: value.d.token, intents: value.d.intents });
          socket.write(frame({ op: 0, s: 1, t: 'READY', d: { session_id: 'fixture-session', resume_gateway_url: gatewayUrl } }));
          socket.write(frame({ op: 0, s: 2, t: 'INTERACTION_CREATE', d: { id: '57', type: 2, token: 'gateway-token', data: { name: 'ping' } } }));
        }
      }
    });
  });
  await new Promise(resolveReady => gateway.listen(0, '127.0.0.1', resolveReady));
  const gatewayAddress = gateway.address();
  assert.ok(gatewayAddress && typeof gatewayAddress !== 'string');
  const gatewayUrl = `wss://localhost:${gatewayAddress.port}`;
  const server = createServer(async (request, response) => {
    let bytes = '';
    for await (const chunk of request) bytes += chunk;
    const body = bytes ? JSON.parse(bytes) : null;
    const path = request.url;
    const auth = request.headers.authorization;
    let status = 200;
    let result = {};
    if (path === '/oauth2/applications/@me' && request.method === 'GET') {
      assert.equal(auth, 'Bot fixture-token'); result = { id: '10' };
    } else if (path === '/gateway/bot' && request.method === 'GET') {
      assert.equal(auth, 'Bot fixture-token'); result = { url: gatewayUrl };
    } else if (path === '/channels/99/messages') {
      forbidden += 1; status = 403;
    } else if (body?.content === 'retry me' && !retried) {
      retried = true; status = 429; result = { retry_after: 0.001 };
    } else {
      delivered.push({ method: request.method, path, body, auth });
      if (path?.endsWith('/callback') || request.method === 'PUT') status = 204;
    }
    response.writeHead(status, { 'Content-Type': 'application/json' });
    response.end(status === 204 ? undefined : JSON.stringify(result));
  });
  t.after(() => new Promise(resolveClosed => server.close(resolveClosed)));
  await new Promise(resolveReady => server.listen(0, '127.0.0.1', resolveReady));
  const address = server.address();
  assert.ok(address && typeof address !== 'string');
  const root = resolve(import.meta.dirname, '../..');
  const child = spawn('docker', ['run', '--rm', '--network', 'host', '-v', `${root}:/src:ro`, '-w', '/tmp',
    '-v', `${directory}:/fixture:ro`, '-e', 'SSL_CERT_FILE=/fixture/cert.pem',
    '-e', 'DISCORD_TOKEN=fixture-token', '-e', `DISCORD_API_BASE=http://127.0.0.1:${address.port}`,
    process.env.FORGE_DISCORD_TEST_IMAGE ?? 'forge-discord-native-builder:validation', '/src/build/discord-native/library-bot'],
    { stdio: ['ignore', 'pipe', 'pipe'] });
  t.after(() => { if (child.exitCode === null) child.kill(); });
  let output = '';
  child.stdout.on('data', chunk => { output += chunk; });
  child.stderr.on('data', chunk => { output += chunk; });
  const code = await new Promise((resolveDone, reject) => { child.once('error', reject); child.once('close', resolveDone); });
  assert.equal(code, 0, output);
  assert.equal(forbidden, 4101);
  assert.equal(retried, true);
  assert.deepEqual(delivered.slice(0, 2).map(item => [item.method, item.path, item.body]), [
    ['POST', '/applications/10/commands', { name: 'ping', description: 'Reply with Pong!', options: [] }],
    ['POST', '/applications/10/guilds/12/commands', { name: 'echo', description: 'Repeat text', options: [
      { name: 'text', description: 'What to say', type: 3, required: true },
    ] }],
  ]);
  const callbacks = delivered.filter(item => item.path?.endsWith('/callback'));
  assert.deepEqual(callbacks.map(item => [item.method, item.path, item.auth, item.body]), [
    ['POST', '/interactions/56/event%2Ftoken/callback', undefined, { type: 4, data: {
      content: '한글 "hello" @everyone', allowed_mentions: { parse: [], replied_user: false },
    } }],
    ['POST', '/interactions/56/event%2Ftoken/callback', undefined, { type: 4, data: {
      content: 'private', allowed_mentions: { parse: [], replied_user: false }, flags: 64,
    } }],
    ['POST', '/interactions/56/event%2Ftoken/callback', undefined, { type: 5, data: {} }],
    ['POST', '/interactions/57/gateway-token/callback', undefined, { type: 4, data: {
      content: 'loop pong', allowed_mentions: { parse: [], replied_user: false },
    } }],
  ]);
  assert.deepEqual(delivered.filter(item => item.path === '/channels/34/messages').map(item => item.body), [
    { content: 'thread reply', allowed_mentions: { parse: [], replied_user: false }, message_reference: { message_id: '78', fail_if_not_exists: false } },
    { content: 'retry me', allowed_mentions: { parse: [], replied_user: false } },
    { content: 'after error', allowed_mentions: { parse: [], replied_user: false } },
    { content: 'after reset', allowed_mentions: { parse: [], replied_user: false } },
  ]);
  assert.deepEqual(delivered.filter(item => item.method === 'PATCH').map(item => [item.path, item.auth, item.body.content]), [
    ['/webhooks/10/event%2Ftoken/messages/@original', undefined, 'done'],
  ]);
  assert.equal(delivered.filter(item => item.path === '/channels/34/messages/78/reactions/%E2%9C%85/@me' && item.method === 'PUT').length, 1);
  assert.ok(delivered.filter(item => item.path?.startsWith('/channels/')).every(item => item.auth === 'Bot fixture-token'));
  assert.deepEqual(identified, [{ token: 'fixture-token', intents: 0 }]);
  assert.equal(delivered.length, 12);
});
