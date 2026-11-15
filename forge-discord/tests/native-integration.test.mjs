import test from 'node:test';
import assert from 'node:assert/strict';
import { access, writeFile, mkdir } from 'node:fs/promises';
import { join } from 'node:path';
import { fixture, command, message, reaction, ids } from './native-fixture.mjs';

test('new server setup persists limits and another process can read them', async t => {
  const state = await fixture(t);
  const result = await state.run(command('setup', { enabled: true, messages: 10, window: 8, mentions: 5, timeout: 0 }, 'automod'));
  assert.equal(result.code, 0, result.output);
  const settings = await state.config();
  assert.equal(settings.guildId, ids.guild);
  assert.equal(settings.moderation.enabled, true);
  assert.equal(settings.moderation.messageLimit, 10);
  assert.equal(settings.moderation.timeoutSeconds, 0);
  assert.equal((await state.run(command('setup', {}, 'show'))).code, 0);
  assert.equal(state.operations[0].body.data.flags, 64);
});

test('fresh permission lookup rejects a non-manager before settings are saved', async t => {
  const state = await fixture(t); state.manager = false;
  assert.equal((await state.run(command('setup', { enabled: true }, 'automod'))).code, 0);
  await assert.rejects(access(join(state.directory, 'config', `${ids.guild}.json`)));
});

test('reaction binding is applied and removed in gateway order without a restart', async t => {
  const state = await fixture(t);
  const result = await state.run([command('setup', { channel: ids.channel, message: ids.message, emoji: '✅', role: ids.role }, 'reaction-add'), reaction(), reaction(false)]);
  assert.equal(result.code, 0, result.output);
  assert.deepEqual(state.operations.filter(item => item.path.endsWith(`/roles/${ids.role}`)).map(item => item.method), ['PUT', 'DELETE']);
});

test('unrelated emoji cannot change a member role', async t => {
  const state = await fixture(t);
  assert.equal((await state.run([command('setup', { channel: ids.channel, message: ids.message, emoji: '✅', role: ids.role }, 'reaction-add'), reaction(true, '❌')])).code, 0);
  assert.equal(state.operations.filter(item => item.path.endsWith(`/roles/${ids.role}`)).length, 0);
});

for (const unsafe of ['privileged', 'hierarchy', 'managed', 'channel']) {
  test(`reaction registration rejects ${unsafe} role/channel facts`, async t => {
    const state = await fixture(t);
    const role = state.roles.find(item => item.id === ids.role);
    switch (unsafe) {
      case 'privileged': role.permissions = '8'; break;
      case 'hierarchy': role.position = 30; break;
      case 'managed': role.managed = true; break;
      case 'channel': state.channel.permission_overwrites.push({ id: ids.bot, type: 1, deny: '65536', allow: '0' }); break;
    }
    assert.equal((await state.run(command('setup', { channel: ids.channel, message: ids.message, emoji: '✅', role: ids.role }, 'reaction-add'))).code, 0);
    await assert.rejects(access(join(state.directory, 'config', `${ids.guild}.json`)));
  });
}

test('missing reaction message cannot create a saved binding', async t => {
  const state = await fixture(t);state.messageMissing = true;
  assert.equal((await state.run(command('setup', { channel: ids.channel, message: ids.message, emoji: '✅', role: ids.role }, 'reaction-add'))).code, 0);
  await assert.rejects(access(join(state.directory, 'config', `${ids.guild}.json`)));
});

test('saved binding removal stops later role changes', async t => {
  const state = await fixture(t);
  const result = await state.run([command('setup', { channel: ids.channel, message: ids.message, emoji: '✅', role: ids.role }, 'reaction-add'),
    command('setup', { role: ids.role }, 'reaction-remove'), reaction()]);
  assert.equal(result.code, 0, result.output);
  assert.deepEqual((await state.config()).reactionRoles, []);
  assert.equal(state.operations.filter(item => item.path.endsWith(`/roles/${ids.role}`)).length, 0);
});

test('normalized word setup deletes blocked content and applies a native timeout', async t => {
  const state = await fixture(t);
  const result = await state.run([command('setup', { word: 'ＦＵＣＫ' }, 'word-add'), command('setup', { enabled: true }, 'automod'), message('fuck')]);
  assert.equal(result.code, 0, result.output);
  assert.equal(state.operations.filter(item => item.method === 'DELETE' && item.path.endsWith(`/messages/${ids.message}`)).length, 1);
  const timeout = state.operations.find(item => item.method === 'PATCH' && item.path.endsWith(`/members/${ids.target}`));
  assert.ok(Date.parse(timeout.body.communication_disabled_until) > Date.now());
});

test('automatic moderation starts disabled and can be disabled dynamically', async t => {
  const state = await fixture(t);
  assert.equal((await state.run([message('fuck'), command('setup', { word: 'fuck' }, 'word-add'),
    command('setup', { enabled: true }, 'automod'), command('setup', { enabled: false }, 'automod'), message('fuck')])).code, 0);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 0);
});

test('spam triggers exactly at the configured threshold', async t => {
  const state = await fixture(t);
  assert.equal((await state.run([command('setup', { enabled: true, messages: 3, timeout: 0 }, 'automod'), message('one'), message('two'), message('three')])).code, 0);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 1);
});

test('partial edits fetch message content without adding a spam event', async t => {
  const state = await fixture(t);
  const edit = { t: 'MESSAGE_UPDATE', d: { guild_id: ids.guild, channel_id: ids.channel, id: ids.message } };
  const result = await state.run([command('setup', { word: 'fuck' }, 'word-add'), command('setup', { enabled: true, messages: 2, timeout: 0 }, 'automod'), message('hello'), edit]);
  assert.equal(result.code, 0, result.output);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 1);
});

test('failed message deletion never escalates to a timeout', async t => {
  const state = await fixture(t);state.failDelete = true;
  const result = await state.run([command('setup', { word: 'fuck' }, 'word-add'), command('setup', { enabled: true }, 'automod'), message('fuck')]);
  assert.equal(result.code, 1);
  assert.equal(state.operations.filter(item => item.path.endsWith(`/members/${ids.target}`) && item.method === 'PATCH').length, 0);
});

test('thread moderation inherits parent channel permissions', async t => {
  const state = await fixture(t);
  state.channel = { id: ids.channel, guild_id: ids.guild, type: 11, parent_id: state.parent.id };
  const result = await state.run([command('setup', { word: 'fuck' }, 'word-add'), command('setup', { enabled: true, timeout: 0 }, 'automod'), message('fuck')]);
  assert.equal(result.code, 0, result.output);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 1);
});

test('timeout transport failure reports a failed event after successful deletion', async t => {
  const state = await fixture(t);state.failTimeout = true;
  const result = await state.run([command('setup', { word: 'fuck' }, 'word-add'), command('setup', { enabled: true }, 'automod'), message('fuck')]);
  assert.equal(result.code, 1);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 1);
});

for (const name of ['kick', 'ban', 'timeout']) {
  test(`authorized ${name} reaches the Discord REST transport`, async t => {
    const state = await fixture(t);
    const result = await state.run(command(name, { member: ids.target, minutes: 5 }));
    assert.equal(result.code, 0, result.output);
    const expected = { kick: 'DELETE', ban: 'PUT', timeout: 'PATCH' }[name];
    assert.equal(state.operations.filter(item => item.method === expected && (item.path.endsWith(`/members/${ids.target}`) || item.path.endsWith(`/bans/${ids.target}`))).length, 1);
  });
}

test('equal role hierarchy prevents moderation', async t => {
  const state = await fixture(t);state.roles.find(item => item.id === ids.role).position = 20;
  assert.equal((await state.run(command('kick', { member: ids.target }))).code, 0);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 0);
});

test('purge omits messages older than fourteen days', async t => {
  const state = await fixture(t);
  const recent = String((BigInt(Date.now() - 1420070400000) << 22n));
  state.recentMessages.push({ id: recent }, { id: ids.message });
  assert.equal((await state.run(command('purge', { count: 10 }))).code, 0);
  assert.deepEqual(state.operations.filter(item => item.method === 'DELETE').map(item => item.path), [`/channels/${ids.channel}/messages/${recent}`]);
});

test('corrupt saved configuration fails closed before performing moderation', async t => {
  const state = await fixture(t);
  await mkdir(join(state.directory, 'config'));await writeFile(join(state.directory, 'config', `${ids.guild}.json`), '{broken');
  await state.run(command('kick', { member: ids.target }));
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 0);
});

test('long Unicode settings previews keep a valid character boundary', async t => {
  const state = await fixture(t);
  assert.equal((await state.run(command('setup', { enabled: false }, 'automod'))).code, 0);
  const settings = await state.config();settings.moderation.blockedTerms = Array(200).fill('가나다라마바사');
  await state.seed(settings);
  const result = await state.run(command('setup', {}, 'show'));
  assert.equal(result.code, 0, result.output);
  const output = state.operations.at(-1).body.content;
  assert.equal(output.includes('\uFFFD'), false);
  assert.ok(output.length < 2000);
});

test('role exemptions take effect and can be removed dynamically', async t => {
  const state = await fixture(t);
  const result = await state.run([command('setup', { word: 'fuck' }, 'word-add'),
    command('setup', { enabled: true, timeout: 0 }, 'automod'), command('setup', { role: ids.role, exempt: true }, 'exempt-role'),
    message('fuck'), command('setup', { role: ids.role, exempt: false }, 'exempt-role'), message('fuck')]);
  assert.equal(result.code, 0, result.output);
  assert.equal(state.operations.filter(item => item.method === 'DELETE').length, 1);
});

test('audit channel receives a saved command outcome without enabling mentions', async t => {
  const state = await fixture(t);
  const result = await state.run(command('setup', { channel: ids.channel }, 'log'));
  assert.equal(result.code, 0, result.output);
  const audit = state.operations.find(item => item.method === 'POST' && item.path === `/channels/${ids.channel}/messages`);
  assert.deepEqual(audit.body.allowed_mentions, { parse: [] });
});
