import test from 'node:test';
import assert from 'node:assert/strict';
import { PermissionsBitField, PermissionFlagsBits } from 'discord.js';
import { AutoModeration } from '../dist/automod.js';
import { configSchema } from '../dist/config.js';

function fixture(moderation = {}) {
  const guildId = '123456789012345678';
  const operations = [];
  const config = configSchema.parse({ guildId, moderation: { blockedTerms: ['fuck'], messageLimit: 3, ...moderation } });
  const guild = { id: guildId, ownerId: '123456789012345699' };
  const member = { id: '123456789012345690', guild, user: { bot: false },
    permissions: new PermissionsBitField(), roles: { highest: { position: 1 }, cache: new Map() },
    async timeout(duration) { operations.push(['timeout', duration]); } };
  const botPermissions = new PermissionsBitField([PermissionFlagsBits.ManageMessages, PermissionFlagsBits.ModerateMembers]);
  const bot = { id: '123456789012345691', guild, user: { bot: true },
    permissions: botPermissions, permissionsIn() { return botPermissions; }, roles: { highest: { position: 10 } } };
  guild.members = { async fetch() { return member; }, async fetchMe() { return bot; } };
  const message = { id: '123456789012345681', guildId, guild, channelId: '123456789012345680', channel: {},
    partial: false, inGuild() { return true; }, author: { id: member.id, bot: false }, content: 'hello',
    mentions: { users: new Map(), roles: new Map(), everyone: false },
    async delete() { operations.push(['delete']); } };
  return { service: new AutoModeration(config), message, member, bot, operations };
}

test('deletes normalized profanity and applies configured timeout through SDK', async () => {
  const state = fixture();
  const result = await state.service.handle({ ...state.message, content: 'ＦＵＣＫ' });
  assert.match(result, /blocked-term/);
  assert.deepEqual(state.operations, [['delete'], ['timeout', 300000]]);
});
test('checks edited content without counting repeated edits as spam', async () => {
  const state = fixture();
  await state.service.handle(state.message);
  await state.service.handle(state.message, true);
  await state.service.handle(state.message, true);
  assert.deepEqual(state.operations, []);
  await state.service.handle({ ...state.message, content: 'fuck' }, true);
  assert.deepEqual(state.operations, [['delete'], ['timeout', 300000]]);
});
test('deletes at spam threshold while preserving earlier messages', async () => {
  const state = fixture({ timeoutSeconds: 0 });
  await state.service.handle(state.message);
  await state.service.handle(state.message);
  assert.deepEqual(state.operations, []);
  assert.match(await state.service.handle(state.message), /type=spam/);
  assert.deepEqual(state.operations, [['delete']]);
});
test('deletes excessive mentions but never times out an administrator', async () => {
  const state = fixture();
  state.member.permissions.add(PermissionFlagsBits.Administrator);
  // Administrator is explicitly exempt from automated management.
  await state.service.handle({ ...state.message, mentions: { ...state.message.mentions, everyone: true } });
  assert.deepEqual(state.operations, []);
  state.member.permissions.remove(PermissionFlagsBits.Administrator);
  state.member.roles.highest.position = 10;
  await state.service.handle({ ...state.message, mentions: { ...state.message.mentions, everyone: true } });
  assert.deepEqual(state.operations, [['delete']]);
});
test('does not mutate messages when bot lacks deletion permission', async () => {
  const state = fixture();
  state.bot.permissions.remove(PermissionFlagsBits.ManageMessages);
  await state.service.handle({ ...state.message, content: 'fuck' });
  assert.deepEqual(state.operations, []);
});
