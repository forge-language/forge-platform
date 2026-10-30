import { PermissionFlagsBits } from 'discord.js';
import type { Message, PartialMessage } from 'discord.js';
import { automod } from './policy.js';
import { normalize } from './config.js';
import type { Config } from './config.js';
import { commandAllowed } from './facts.js';

export class AutoModeration {
  // Bounded in-memory event timestamps. Restarts reset the short spam window.
  private readonly windows = new Map<string, readonly number[]>();
  constructor(private readonly config: Config) {}

  async handle(input: Message | PartialMessage, edit = false): Promise<string> {
    if (!this.config.moderation.enabled || input.guildId !== this.config.guildId) return '';
    const message = input.partial ? await input.fetch() : input;
    if (!message.inGuild() || message.author.bot) return '';
    const [member, bot] = await Promise.all([message.guild.members.fetch(message.author.id), message.guild.members.fetchMe()]);
    const now = Date.now();
    const cutoff = now - this.config.moderation.windowSeconds * 1000;
    for (const [key, times] of this.windows) {
      if ((times.at(-1) ?? 0) < cutoff) this.windows.delete(key);
    }
    const key = `${message.guild.id}/${member.id}`;
    const times = (this.windows.get(key) ?? []).filter(time => time >= cutoff);
    if (!edit) times.push(now);
    this.windows.set(key, times.slice(-this.config.moderation.messageLimit));
    if (this.windows.size > 10000) {
      const oldest = this.windows.keys().next().value;
      if (oldest) this.windows.delete(oldest);
    }
    const decision = automod(JSON.stringify({
      guild: message.guild.id, configuredGuild: this.config.guildId,
      memberBot: Number(member.user.bot),
      botManageMessages: Number(bot.permissionsIn(message.channel).has(PermissionFlagsBits.ManageMessages)),
      memberOwner: Number(member.id === message.guild.ownerId),
      memberManageMessages: Number(member.permissions.has(PermissionFlagsBits.ManageMessages)),
      exemptRole: Number(this.config.moderation.exemptRoleIds.some(roleId => member.roles.cache.has(roleId))),
      exemptChannel: Number(this.config.moderation.exemptChannelIds.includes(message.channelId)),
      content: normalize(message.content), terms: this.config.moderation.blockedTerms.map(normalize).join('\n'),
      messageCount: times.length, messageLimit: this.config.moderation.messageLimit,
      mentions: message.mentions.users.size + message.mentions.roles.size + Number(message.mentions.everyone) * this.config.moderation.mentionLimit,
      mentionLimit: this.config.moderation.mentionLimit,
    }));
    if (decision === 0n) return '';
    await message.delete();
    if (this.config.moderation.timeoutSeconds > 0 && commandAllowed({ command: 'timeout', guildId: message.guild.id,
      config: this.config, actor: bot, bot, target: member, channel: message.channel })) {
      await member.timeout(this.config.moderation.timeoutSeconds * 1000, decision === 1n ? 'Forge: blocked term' : 'Forge: spam/mention limit');
    }
    return `automod type=${decision === 1n ? 'blocked-term' : 'spam'} member=${member.id} message=${message.id}`;
  }
}
