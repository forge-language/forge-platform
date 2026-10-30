import test from 'node:test';
import assert from 'node:assert/strict';
import { authorize, automod, reaction_role } from '../dist/policy.js';
import { configSchema, normalize } from '../dist/config.js';
import { readFileSync } from 'node:fs';

const command = {
  command: 'timeout', guild: 'guild-a', configuredGuild: 'guild-a',
  actorManageMessages: 1, botManageMessages: 1, actorManageGuild: 1, botManageRoles: 1,
  actorModerate: 1, botModerate: 1, actorKick: 1, botKick: 1, actorBan: 1, botBan: 1,
  actorOwner: 0, actorPosition: 20, botPosition: 30, targetPosition: 10,
  targetPresent: 1, targetOwner: 0, targetSelf: 0, targetBot: 0, targetAdmin: 0,
};
for (const name of ['timeout', 'kick', 'ban', 'purge', 'roles']) {
  test(`allows ${name} when permissions and hierarchy permit`, () => {
    // Given authorized protocol facts; When evaluated by compiled Forge; Then permit.
    assert.equal(authorize(JSON.stringify({ ...command, command: name })), 1n);
  });
}
for (const [field, value] of Object.entries({ targetPresent: 0, actorModerate: 0, botModerate: 0, targetOwner: 1,
  targetSelf: 1, targetBot: 1, targetAdmin: 1, actorPosition: 10, botPosition: 10, guild: 'guild-b' })) {
  test(`rejects timeout when ${field} is disallowed`, () => {
    // Given one unsafe permission or hierarchy fact; When evaluated; Then reject.
    assert.equal(authorize(JSON.stringify({ ...command, [field]: value })), 0n);
  });
}
test('permits owner to moderate a higher member when bot outranks target', () => {
  // Given owner with a low displayed role; When kicking; Then owner bypasses own role order.
  assert.equal(authorize(JSON.stringify({ ...command, command: 'kick', actorPosition: 0, actorOwner: 1 })), 1n);
});
test('rejects unknown commands', () => {
  // Given an arbitrary command; When evaluated; Then reject.
  assert.equal(authorize(JSON.stringify({ ...command, command: 'grant-admin' })), 0n);
});

const reaction = {
  guild: 'guild-a', configuredGuild: 'guild-a', channel: 'channel-a', bindingChannel: 'channel-a',
  message: 'message-a', bindingMessage: 'message-a', emoji: '✅', bindingEmoji: '✅', bindingRole: 'role-a',
  memberBot: 0, roleManaged: 0, roleDangerous: 0, roleEveryone: 0,
  botManageRoles: 1, botPosition: 30, rolePosition: 10,
};
test('resolves exact configured reaction to its allowlisted role', () => {
  // Given exact binding facts; When evaluated; Then return only the configured role.
  assert.equal(reaction_role(JSON.stringify(reaction)), 'role-a');
});
for (const [field, value] of Object.entries({ guild: 'guild-b', channel: 'channel-b', message: 'message-b',
  emoji: '❌', memberBot: 1, roleManaged: 1, roleDangerous: 1, roleEveryone: 1, botManageRoles: 0, rolePosition: 30 })) {
  test(`rejects reaction role when ${field} does not permit it`, () => {
    // Given one mismatched or unsafe fact; When evaluated; Then no role can change.
    assert.equal(reaction_role(JSON.stringify({ ...reaction, [field]: value })), '');
  });
}

const message = { guild: 'guild-a', configuredGuild: 'guild-a', memberBot: 0,
  memberOwner: 0, memberManageMessages: 0, exemptRole: 0, exemptChannel: 0,
  botManageMessages: 1, content: 'hello forge', terms: '시발\nfuck', messageCount: 1, messageLimit: 6, mentions: 0, mentionLimit: 8 };
test('leaves ordinary messages alone', () => {
  // Given ordinary content; When evaluated; Then no moderation.
  assert.equal(automod(JSON.stringify(message)), 0n);
});
for (const content of ['시발이다', 'ＦＵＣＫ this']) {
  test(`detects normalized blocked content: ${content}`, () => {
    // Given blocked content including compatibility characters; When normalized/evaluated; Then blocked-term action.
    assert.equal(automod(JSON.stringify({ ...message, content: normalize(content) })), 1n);
  });
}
for (const patch of [{ messageCount: 6 }, { mentions: 8 }]) {
  test(`detects spam at configured threshold ${JSON.stringify(patch)}`, () => {
    // Given spam at the boundary; When evaluated; Then spam action.
    assert.equal(automod(JSON.stringify({ ...message, ...patch })), 2n);
  });
}
for (const patch of [{ memberOwner: 1 }, { memberManageMessages: 1 }, { exemptRole: 1 }, { exemptChannel: 1 }, { memberBot: 1 }, { guild: 'guild-b' }, { botManageMessages: 0 }]) {
  test(`skips protected messages ${JSON.stringify(patch)}`, () => {
    // Given blocked content with a bypass fact; When evaluated; Then no action.
    assert.equal(automod(JSON.stringify({ ...message, content: 'fuck', ...patch })), 0n);
  });
}
const example = JSON.parse(readFileSync(new URL('../config.example.json', import.meta.url), 'utf8'));
test('accepts documented configuration', () => {
  // Given example configuration; When parsed at the boundary; Then a typed guild config.
  assert.equal(configSchema.parse(example).guildId, '123456789012345678');
});
test('rejects duplicate role bindings that would cause conflicting removals', () => {
  // Given two different panels sharing one role; When parsed; Then reject ambiguity.
  const config = { ...example, reactionRoles: [...example.reactionRoles,
    { ...example.reactionRoles[0], messageId: '123456789012345699' }] };
  assert.equal(configSchema.safeParse(config).success, false);
});
test('rejects a zero spam limit', () => {
  // Given an invalid limit; When parsed; Then reject configuration.
  assert.equal(configSchema.safeParse({ ...example, moderation: { ...example.moderation, messageLimit: 0 } }).success, false);
});
