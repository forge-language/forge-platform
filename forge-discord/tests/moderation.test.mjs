import test from 'node:test';
import assert from 'node:assert/strict';
import { PermissionsBitField, PermissionFlagsBits } from 'discord.js';
import { moderate } from '../dist/moderation.js';
import { configSchema } from '../dist/config.js';

function fixture(command) {
  const guildId = '123456789012345678';
  const operations = [];
  const replies = [];
  const permissions = new PermissionsBitField([PermissionFlagsBits.ManageMessages,
    PermissionFlagsBits.ModerateMembers, PermissionFlagsBits.KickMembers, PermissionFlagsBits.BanMembers]);
  const guild = { id: guildId, ownerId: 'owner' };
  function member(id, position) {
    return { id, guild, user: { bot: false }, permissions: new PermissionsBitField(permissions),
      permissionsIn() { return this.permissions; }, roles: { highest: { position } },
      async timeout(duration) { operations.push(['timeout', duration]); },
      async kick() { operations.push(['kick']); }, async ban() { operations.push(['ban']); } };
  }
  const actor = member('actor', 20);
  const bot = member('bot', 30);
  const target = member('target', 10);
  guild.members = { async fetch(id) { return id === actor.id ? actor : target; }, async fetchMe() { return bot; } };
  const channel = { async bulkDelete(count, filterOld) { operations.push(['purge', count, filterOld]); return { size: 2 }; } };
  const interaction = { guildId, guild, user: { id: actor.id }, commandName: command, channel,
    inCachedGuild() { return true; }, async deferReply() {}, async editReply(reply) { replies.push(reply); },
    options: { getUser() { return ['timeout', 'kick', 'ban'].includes(command) ? { id: target.id } : null; },
      getString() { return null; }, getInteger(name) { return name === 'count' ? 10 : 5; } } };
  return { config: configSchema.parse({ guildId, moderation: { blockedTerms: [] } }), interaction, actor, target, operations, replies };
}
for (const [command, expected] of [['timeout', ['timeout', 300000]], ['kick', ['kick']], ['ban', ['ban']], ['purge', ['purge', 10, true]]]) {
  test(`dispatches authorized /${command} to Discord adapter`, async () => {
    const state = fixture(command);
    assert.match(await moderate(state.interaction, state.config), new RegExp(`^${command} actor=`));
    assert.deepEqual(state.operations, [expected]);
    assert.equal(state.replies.length, 1);
  });
}
test('rejects low-permission actor before mutating Discord', async () => {
  const state = fixture('kick');
  state.actor.permissions.remove(PermissionFlagsBits.KickMembers);
  assert.equal(await moderate(state.interaction, state.config), '');
  assert.deepEqual(state.operations, []);
  assert.match(state.replies[0], /권한/);
});
test('rejects equal target hierarchy before mutating Discord', async () => {
  const state = fixture('ban');
  state.target.roles.highest.position = state.actor.roles.highest.position;
  assert.equal(await moderate(state.interaction, state.config), '');
  assert.deepEqual(state.operations, []);
});
test('seeds only allowlisted panel emoji after verifying role safety', async () => {
  const state = fixture('roles');
  state.actor.permissions.add(PermissionFlagsBits.ManageGuild);
  const bot = await state.interaction.guild.members.fetchMe();
  bot.permissions.add(PermissionFlagsBits.ManageRoles);
  const roleId = '123456789012345682';
  const messageId = '123456789012345681';
  const channelId = '123456789012345680';
  const role = { id: roleId, guild: state.interaction.guild, managed: false, position: 1, permissions: new PermissionsBitField() };
  state.interaction.guild.roles = { async fetch() { return role; } };
  state.interaction.guild.channels = { async fetch(id) {
    assert.equal(id, channelId);
    return { isTextBased() { return true; }, messages: { async fetch(id) {
      assert.equal(id, messageId);
      return { async react(emoji) { state.operations.push(['react', emoji]); } };
    } } };
  } };
  state.interaction.options.getString = name => name === 'message' ? messageId : null;
  const config = configSchema.parse({ ...state.config, reactionRoles: [{ channelId, messageId, roleId, emoji: '✅' }] });
  assert.match(await moderate(state.interaction, config), /^roles actor=/);
  assert.deepEqual(state.operations, [['react', '✅']]);
  role.permissions.add(PermissionFlagsBits.Administrator);
  state.operations.length = 0;
  assert.equal(await moderate(state.interaction, config), '');
  assert.deepEqual(state.operations, []);
});
