import { createServer } from 'node:http';
import { mkdtemp, writeFile, readFile, mkdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawn } from 'node:child_process';

export const ids = { guild: '123456789012345678', channel: '123456789012345680', message: '123456789012345681',
  role: '123456789012345682', actor: '123456789012345690', bot: '123456789012345691', target: '123456789012345692',
  managerRole: '123456789012345693', botRole: '123456789012345694', owner: '123456789012345699' };
const root = resolve(import.meta.dirname, '../..');

export function command(name, values = {}, action) {
  let options = Object.entries(values).map(([key, value]) => ({ name: key, value }));
  if (action) options = [{ name: action, type: 1, options }];
  return { t: 'INTERACTION_CREATE', d: { id: ids.message, token: 'fixture-interaction-token', type: 2,
    guild_id: ids.guild, channel_id: ids.channel, member: { user: { id: ids.actor } }, data: { name, options } } };
}
export function message(content, patch = {}) {
  return { t: 'MESSAGE_CREATE', d: { id: ids.message, guild_id: ids.guild, channel_id: ids.channel,
    author: { id: ids.target, bot: false }, content, mentions: [], mention_roles: [], mention_everyone: false, ...patch } };
}
export function reaction(add = true, emoji = '✅') {
  return { t: add ? 'MESSAGE_REACTION_ADD' : 'MESSAGE_REACTION_REMOVE', d: { guild_id: ids.guild,
    channel_id: ids.channel, message_id: ids.message, user_id: ids.target, emoji: { id: null, name: emoji } } };
}

export async function fixture(t) {
  const directory = await mkdtemp(join(tmpdir(), 'forge-discord-native-'));
  const operations = [];
  const roles = [
    { id: ids.guild, permissions: '3072', position: 0, managed: false },
    { id: ids.managerRole, permissions: String(32n | 8192n | 2n | 4n | (1n << 40n)), position: 20, managed: false },
    { id: ids.botRole, permissions: String(32n | 8192n | 2n | 4n | (1n << 40n) | (1n << 28n) | 65536n | 64n), position: 30, managed: false },
    { id: ids.role, permissions: '0', position: 1, managed: false },
  ];
  const state = { manager: true, failDelete: false, failTimeout: false, messageMissing: false,
    roles, operations, directory, channel: { id: ids.channel, guild_id: ids.guild, type: 0, permission_overwrites: [] },
    recentMessages: [], parent: { id: '123456789012345677', guild_id: ids.guild, type: 0, permission_overwrites: [] } };
  const server = createServer(async (request, response) => {
    let bytes = '';
    for await (const chunk of request) bytes += chunk;
    const body = bytes ? JSON.parse(bytes) : null;
    const path = new URL(request.url ?? '/', 'http://fixture').pathname;
    const method = request.method;
    let status = 200; let data = {};
    if (method !== 'GET') {
      operations.push({ method, path, body });
      status = method === 'POST' && path.endsWith('/callback') ? 204 : 200;
      if (state.failDelete && method === 'DELETE') status = 403;
      if (state.failTimeout && method === 'PATCH' && path.endsWith(`/members/${ids.target}`)) status = 403;
    } else if (path === `/guilds/${ids.guild}`) data = { id: ids.guild, owner_id: ids.owner };
    else if (path === `/guilds/${ids.guild}/roles`) data = roles;
    else if (path.startsWith(`/guilds/${ids.guild}/members/`)) {
      const id = path.split('/').at(-1);
      data = { user: { id, bot: id === ids.bot }, roles: id === ids.bot ? [ids.botRole]
        : id === ids.actor && state.manager ? [ids.managerRole] : [ids.role] };
    } else if (path === `/channels/${ids.channel}`) data = state.channel;
    else if (path === `/channels/${state.parent.id}`) data = state.parent;
    else if (path === `/channels/${ids.channel}/messages`) data = state.recentMessages;
    else if (path === `/channels/${ids.channel}/messages/${ids.message}`) {
      status = state.messageMissing ? 404 : 200;
      data = message('fuck').d;
    } else status = 404;
    response.writeHead(status, { 'Content-Type': 'application/json' });
    response.end(status === 204 ? undefined : JSON.stringify(data));
  });
  await new Promise(resolveReady => server.listen(0, '127.0.0.1', resolveReady));
  const address = server.address();
  if (!address || typeof address === 'string') throw new TypeError('Expected TCP fixture address');
  t.after(async () => { await new Promise(resolveClosed => server.close(resolveClosed)); await rm(directory, { recursive: true, force: true }); });
  state.run = async events => {
    await writeFile(join(directory, 'events.json'), JSON.stringify(events));
    const args = ['run', '--rm', '--network', 'host', '--user', `${process.getuid()}:${process.getgid()}`,
      '-v', `${root}:/src:ro`, '-v', `${directory}:/case`, '-w', '/case',
      '-e', 'DISCORD_TOKEN=fixture-bot-token', '-e', `DISCORD_CLIENT_ID=${ids.bot}`,
      '-e', `DISCORD_API_BASE=http://127.0.0.1:${address.port}`, '-e', 'BOT_CONFIG_DIR=/case/config',
      '-e', 'BOT_CONFIG=/case/no-legacy', process.env.FORGE_DISCORD_TEST_IMAGE ?? 'forge-discord-native-builder:validation',
      '/src/forge-discord/build/forge-discord', '--event', '/case/events.json'];
    return new Promise((resolveDone, reject) => {
      const child = spawn('docker', args, { stdio: ['ignore', 'pipe', 'pipe'] });
      let output = '';
      child.stdout.on('data', chunk => { output += chunk; });child.stderr.on('data', chunk => { output += chunk; });
      child.on('error', reject);child.on('close', code => resolveDone({ code, output }));
    });
  };
  state.config = async () => JSON.parse(await readFile(join(directory, 'config', `${ids.guild}.json`), 'utf8'));
  state.seed = async value => { await mkdir(join(directory, 'config'), { recursive: true }); await writeFile(join(directory, 'config', `${ids.guild}.json`), JSON.stringify(value)); };
  return state;
}
