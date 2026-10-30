import { PermissionFlagsBits } from 'discord.js';
import type { GuildMember, GuildBasedChannel, Role } from 'discord.js';
import { authorize, reaction_role } from './policy.js';
import type { Config } from './config.js';

type CommandContext = {
  readonly command: string;
  readonly guildId: string;
  readonly config: Config;
  readonly actor: GuildMember;
  readonly bot: GuildMember;
  readonly target: GuildMember | null;
  readonly channel: GuildBasedChannel;
};
export function commandAllowed(context: CommandContext): boolean {
  const { actor, bot, target, channel } = context;
  const actorPermissions = actor.permissionsIn(channel);
  const botPermissions = bot.permissionsIn(channel);
  return authorize(JSON.stringify({
    command: context.command, guild: context.guildId, configuredGuild: context.config.guildId,
    actorManageMessages: Number(actorPermissions.has(PermissionFlagsBits.ManageMessages)),
    botManageMessages: Number(botPermissions.has(PermissionFlagsBits.ManageMessages)),
    actorManageGuild: Number(actor.permissions.has(PermissionFlagsBits.ManageGuild)),
    botManageRoles: Number(bot.permissions.has(PermissionFlagsBits.ManageRoles)),
    actorModerate: Number(actor.permissions.has(PermissionFlagsBits.ModerateMembers)),
    botModerate: Number(bot.permissions.has(PermissionFlagsBits.ModerateMembers)),
    actorKick: Number(actor.permissions.has(PermissionFlagsBits.KickMembers)),
    botKick: Number(bot.permissions.has(PermissionFlagsBits.KickMembers)),
    actorBan: Number(actor.permissions.has(PermissionFlagsBits.BanMembers)),
    botBan: Number(bot.permissions.has(PermissionFlagsBits.BanMembers)),
    actorOwner: Number(actor.id === actor.guild.ownerId),
    actorPosition: actor.roles.highest.position, botPosition: bot.roles.highest.position,
    targetPresent: Number(target !== null),
    targetPosition: target?.roles.highest.position ?? 0,
    targetOwner: Number(target?.id === actor.guild.ownerId),
    targetSelf: Number(target?.id === actor.id), targetBot: Number(target?.user.bot ?? false),
    targetAdmin: Number(target?.permissions.has(PermissionFlagsBits.Administrator) ?? false),
  })) === 1n;
}

// SDK permission bits are protocol facts; Forge decides whether they are safe.
const privileged = [
  PermissionFlagsBits.Administrator, PermissionFlagsBits.ManageGuild,
  PermissionFlagsBits.ManageRoles, PermissionFlagsBits.ManageChannels,
  PermissionFlagsBits.ManageWebhooks, PermissionFlagsBits.ManageMessages,
  PermissionFlagsBits.KickMembers, PermissionFlagsBits.BanMembers,
  PermissionFlagsBits.ModerateMembers, PermissionFlagsBits.MentionEveryone,
] as const;
type ReactionContext = {
  readonly guildId: string;
  readonly channelId: string;
  readonly messageId: string;
  readonly emoji: string;
  readonly memberBot: boolean;
  readonly config: Config;
  readonly bot: GuildMember;
  readonly role: Role;
  readonly binding: Config['reactionRoles'][number];
};
export function reactionRole(context: ReactionContext): string {
  const { binding, bot, role } = context;
  return reaction_role(JSON.stringify({
    guild: context.guildId, configuredGuild: context.config.guildId,
    channel: context.channelId, bindingChannel: binding.channelId,
    message: context.messageId, bindingMessage: binding.messageId,
    emoji: context.emoji, bindingEmoji: binding.emoji, bindingRole: binding.roleId,
    memberBot: Number(context.memberBot), roleManaged: Number(role.managed),
    roleDangerous: Number(role.permissions.any([...privileged])),
    roleEveryone: Number(role.id === role.guild.id),
    botManageRoles: Number(bot.permissions.has(PermissionFlagsBits.ManageRoles)),
    botPosition: bot.roles.highest.position, rolePosition: role.position,
  }));
}
