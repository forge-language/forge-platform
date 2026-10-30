import { Client, Events, GatewayIntentBits, MessageFlags, Partials } from 'discord.js';
import { credentials, loadConfig } from './config.js';
import { AutoModeration } from './automod.js';
import { moderate } from './moderation.js';
import { ReactionRoles } from './reactions.js';

const config = loadConfig();
const environment = credentials();
const client = new Client({
  intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMessages, GatewayIntentBits.GuildMessageReactions, GatewayIntentBits.MessageContent],
  partials: [Partials.Message, Partials.Channel, Partials.Reaction, Partials.User],
  allowedMentions: { parse: [] },
});
const reactions = new ReactionRoles(config);
const moderation = new AutoModeration(config);

// Event boundaries report failures without serializing SDK requests or tokens.
function failure(operation: string, error: unknown): void {
  console.error(JSON.stringify({ event: 'bot.operation_failed', operation,
    errorType: error instanceof Error ? error.name : typeof error }));
}
async function audit(content: string): Promise<void> {
  if (!content) return;
  console.log(JSON.stringify({ event: 'bot.action', detail: content }));
  if (config.logChannelId) {
    try {
      const channel = await client.channels.fetch(config.logChannelId);
      if (channel?.isSendable() && 'guildId' in channel && channel.guildId === config.guildId) await channel.send(content);
    } catch (error) { failure('audit', error); }
  }
}
client.once(Events.ClientReady, ready => {
  console.log(JSON.stringify({ event: 'bot.ready', botId: ready.user.id, guildId: config.guildId }));
});
client.on(Events.Error, error => failure('gateway', error));
client.on(Events.InteractionCreate, interaction => {
  if (!interaction.isChatInputCommand()) return;
  void moderate(interaction, config).then(audit).catch(async error => {
    failure('command', error);
    const content = '실행하지 못했습니다. 봇 권한과 역할 서열을 확인해 주세요.';
    try {
      if (interaction.deferred || interaction.replied) await interaction.editReply(content);
      else await interaction.reply({ content, flags: MessageFlags.Ephemeral });
    } catch (replyError) { failure('command_reply', replyError); }
  });
});
client.on(Events.MessageReactionAdd, (reaction, user) => {
  void reactions.handle(reaction, user, true).catch(error => failure('reaction_add', error));
});
client.on(Events.MessageReactionRemove, (reaction, user) => {
  void reactions.handle(reaction, user, false).catch(error => failure('reaction_remove', error));
});
client.on(Events.MessageCreate, message => {
  void moderation.handle(message).then(audit).catch(error => failure('automod', error));
});
client.on(Events.MessageUpdate, (_old, message) => {
  void moderation.handle(message, true).then(audit).catch(error => failure('automod_edit', error));
});
process.once('SIGTERM', () => client.destroy());
process.once('SIGINT', () => client.destroy());
await client.login(environment.DISCORD_TOKEN);
