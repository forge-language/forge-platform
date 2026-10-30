import type { MessageReaction, PartialMessageReaction, User, PartialUser } from 'discord.js';
import type { Config } from './config.js';
import { reactionRole } from './facts.js';

// Serialize changes to each member so fast add/remove events retain gateway order.
export class ReactionRoles {
  private readonly pending = new Map<string, Promise<void>>();
  constructor(private readonly config: Config) {}

  handle(reaction: MessageReaction | PartialMessageReaction, user: User | PartialUser, add: boolean): Promise<void> {
    const key = `${reaction.message.guildId}/${user.id}`;
    const previous = this.pending.get(key) ?? Promise.resolve();
    const next = previous.then(() => this.apply(reaction, user, add));
    // The caller receives failures; later events still run after a failed operation.
    const settled = next.then(() => undefined, () => undefined);
    this.pending.set(key, settled);
    void settled.then(() => { if (this.pending.get(key) === settled) this.pending.delete(key); });
    return next;
  }

  private async apply(reaction: MessageReaction | PartialMessageReaction, user: User | PartialUser, add: boolean): Promise<void> {
    if (reaction.message.guildId !== this.config.guildId || user.bot) return;
    const emoji = reaction.emoji.id ?? reaction.emoji.name ?? '';
    // Avoid network requests for unconfigured messages. Forge verifies every binding.
    const bindings = this.config.reactionRoles.filter(binding => binding.messageId === reaction.message.id);
    if (bindings.length === 0) return;
    const message = reaction.message.partial ? await reaction.message.fetch() : reaction.message;
    const guild = message.guild;
    if (!guild) return;
    const [member, bot] = await Promise.all([guild.members.fetch(user.id), guild.members.fetchMe()]);
    for (const binding of bindings) {
      const role = await guild.roles.fetch(binding.roleId);
      if (!role) continue;
      const roleId = reactionRole({ config: this.config, binding, bot, role, guildId: guild.id,
        channelId: message.channelId, messageId: message.id, emoji, memberBot: member.user.bot });
      if (!roleId) continue;
      if (add) await member.roles.add(roleId, 'Forge reaction role');
      else await member.roles.remove(roleId, 'Forge reaction removed');
    }
  }
}
