import test from 'node:test';
import assert from 'node:assert/strict';
import { PermissionsBitField, PermissionFlagsBits } from 'discord.js';
import { ReactionRoles } from '../dist/reactions.js';
import { configSchema } from '../dist/config.js';

function fixture() {
  const guildId = '123456789012345678';
  const roleId = '123456789012345682';
  const channelId = '123456789012345680';
  const messageId = '123456789012345681';
  const config = configSchema.parse({ guildId, reactionRoles: [{ channelId, messageId, emoji: '✅', roleId }],
    moderation: { blockedTerms: [] } });
  const roles = new Set();
  const operations = [];
  const role = { id: roleId, position: 1, managed: false, guild: { id: guildId }, permissions: new PermissionsBitField() };
  const member = { id: '123456789012345690', user: { bot: false }, roles: {
    async add(id) { operations.push(`add:${id}`); roles.add(id); },
    async remove(id) { operations.push(`remove:${id}`); roles.delete(id); },
  } };
  const bot = { roles: { highest: { position: 10 } }, permissions: new PermissionsBitField(PermissionFlagsBits.ManageRoles) };
  const guild = { id: guildId, members: { async fetch() { return member; }, async fetchMe() { return bot; } },
    roles: { async fetch() { return role; } } };
  const message = { id: messageId, channelId, guildId, guild, partial: false };
  return { service: new ReactionRoles(config), roles, operations,
    reaction: { message, emoji: { id: null, name: '✅' } }, user: { id: member.id, bot: false } };
}

test('keeps final role absent when add and remove events arrive together', async () => {
  // Given configured role events sharing a member; When both are dispatched without awaiting; Then preserve event order.
  const state = fixture();
  await Promise.all([state.service.handle(state.reaction, state.user, true), state.service.handle(state.reaction, state.user, false)]);
  assert.deepEqual(state.operations, ['add:123456789012345682', 'remove:123456789012345682']);
  assert.equal(state.roles.size, 0);
});
test('never removes a role for another emoji', async () => {
  // Given an unrelated reaction; When removed; Then no role mutation.
  const state = fixture();
  await state.service.handle({ ...state.reaction, emoji: { id: null, name: '❌' } }, state.user, false);
  assert.deepEqual(state.operations, []);
});
test('fetches an uncached message after restart before applying role', async () => {
  // Given an uncached message event; When added; Then message fetch supplies actual channel/guild facts.
  const state = fixture();
  let fetched = false;
  const message = { ...state.reaction.message, partial: true, async fetch() { fetched = true; return state.reaction.message; } };
  await state.service.handle({ ...state.reaction, message }, state.user, true);
  assert.equal(fetched, true);
  assert.deepEqual([...state.roles], ['123456789012345682']);
});
test('continues processing after a failed Discord role request', async () => {
  // Given a rejected API operation; When the next event arrives; Then the queue remains usable.
  const state = fixture();
  const member = await state.reaction.message.guild.members.fetch(state.user.id);
  const original = member.roles.add;
  let fail = true;
  member.roles.add = async id => { if (fail) { fail = false; throw new TypeError('test wire failure'); } return original(id); };
  await assert.rejects(state.service.handle(state.reaction, state.user, true), TypeError);
  await state.service.handle(state.reaction, state.user, true);
  assert.deepEqual([...state.roles], ['123456789012345682']);
});
