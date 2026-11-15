import assert from 'node:assert/strict';
import { createServer as createHttpServer } from 'node:http';
import { createServer as createTlsServer } from 'node:tls';
import { createHash } from 'node:crypto';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';

const root = fileURLToPath(new URL('../../', import.meta.url));
const directory = await mkdtemp(join(tmpdir(), 'forge-native-wire-'));
const sockets = new Set();
const timers = new Set();
let protocolFailure = false;
const http = createHttpServer((request, response) => {
  let body = '';
  request.on('data', chunk => { body += chunk.toString(); });
  request.on('end', () => {
    const valid = request.method === 'POST' && request.headers.authorization === 'Bot fixture-token' && body === '{"ok":true}';
    protocolFailure ||= !valid;
    response.writeHead(valid ? 200 : 400, { 'Content-Type': 'application/json' });
    response.end('{"ok":true}');
  });
});
let tls;
function frame(opcode, payload, final = true) {
  const bytes = Buffer.from(payload);
  assert.ok(bytes.length < 126);
  return Buffer.concat([Buffer.from([(final ? 128 : 0) | opcode, bytes.length]), bytes]);
}
function later(callback, milliseconds) {
  const timer = setTimeout(() => { timers.delete(timer); callback(); }, milliseconds);
  timers.add(timer);
}
try {
  const certificate = spawnSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', join(directory, 'key.pem'),
    '-out', join(directory, 'cert.pem'), '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost', '-days', '1'], { stdio: 'ignore' });
  assert.equal(certificate.status, 0);
  tls = createTlsServer({ key: await readFile(join(directory, 'key.pem')), cert: await readFile(join(directory, 'cert.pem')) }, socket => {
    sockets.add(socket); socket.on('close', () => sockets.delete(socket));
    let received = Buffer.alloc(0); let upgraded = false;
    socket.on('data', chunk => {
      received = Buffer.concat([received, chunk]);
      if (!upgraded) {
        const end = received.indexOf('\r\n\r\n');
        if (end < 0) return;
        const headers = received.subarray(0, end).toString();
        const key = /^Sec-WebSocket-Key:\s*(.+)$/im.exec(headers)?.[1]?.trim();
        if (!key) { protocolFailure = true; socket.destroy(); return; }
        const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
        socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
        received = received.subarray(end + 4); upgraded = true;
      }
      while (received.length >= 2) {
        const opcode = received[0] & 15;
        const count = received[1] & 127;
        if (!(received[1] & 128) || count >= 126) { protocolFailure = true; socket.destroy(); return; }
        if (received.length < count + 6) return;
        const payload = Buffer.from(received.subarray(6, 6 + count));
        for (let index = 0; index < count; index++) payload[index] ^= received[2 + index % 4];
        received = received.subarray(6 + count);
        if (opcode === 10) continue;
        if (opcode !== 1) { protocolFailure = true; socket.destroy(); return; }
        if (payload.toString() === 'hello') {
          socket.write(frame(1, 'split ', false));
          socket.write(frame(9, 'ping'));
          later(() => {
            const continuation = frame(0, 'message');
            socket.write(continuation.subarray(0, 4));
            later(() => socket.write(continuation.subarray(4)), 30);
          }, 120);
        } else if (payload.toString() === 'done') {
          socket.write(frame(8, Buffer.from([3, 232])));
        } else { protocolFailure = true; socket.destroy(); }
      }
    });
    socket.on('error', error => { if (error.code !== 'ECONNRESET') protocolFailure = true; });
  });
  await Promise.all([new Promise(resolveReady => http.listen(0, '127.0.0.1', resolveReady)), new Promise(resolveReady => tls.listen(0, '127.0.0.1', resolveReady))]);
  const httpAddress = http.address(); const tlsAddress = tls.address();
  assert.ok(httpAddress && typeof httpAddress !== 'string' && tlsAddress && typeof tlsAddress !== 'string');
  const args = ['run', '--rm', '--network', 'host', '-e', 'SSL_CERT_FILE=/fixture/cert.pem', '-v', `${root}:/src:ro`, '-v', `${directory}:/fixture:ro`,
    '-w', '/src', process.env.FORGE_DISCORD_TEST_IMAGE ?? 'forge-discord-native-builder:validation',
    resolve(root, process.env.FORGE_DISCORD_TRANSPORT_TEST ?? 'build/discord-native/transport-test').replace(root, '/src/'),
    `http://127.0.0.1:${httpAddress.port}/echo`, `wss://localhost:${tlsAddress.port}/`];
  const status = await new Promise((resolveExit, reject) => {
    const child = spawn('docker', args, { cwd: root, stdio: 'inherit' });
    const timeout = setTimeout(() => { child.kill('SIGTERM'); reject(new Error('Native transport fixture timed out')); }, 20000);
    child.on('error', error => { clearTimeout(timeout); reject(error); });
    child.on('exit', code => { clearTimeout(timeout); resolveExit(code); });
  });
  assert.equal(status, 0); assert.equal(protocolFailure, false);
  console.log('PASS real HTTP and WSS fixture protocol assertions');
} finally {
  for (const timer of timers) clearTimeout(timer);
  for (const socket of sockets) socket.destroy();
  http.closeAllConnections();
  await Promise.all([new Promise(resolveClosed => http.close(resolveClosed)), tls ? new Promise(resolveClosed => tls.close(resolveClosed)) : Promise.resolve()]);
  await rm(directory, { recursive: true, force: true });
}
