import { MessageFlags } from 'discord.js';
import type { ChatInputCommandInteraction } from 'discord.js';
import type { Config } from './config.js';
import { commandAllowed, reactionRole } from './facts.js';

export async function moderate(interaction: ChatInputCommandInteraction, config: Config): Promise<string> {
  if (interaction.guildId !== config.guildId || !interaction.inCachedGuild() || !interaction.channel) {
    await interaction.reply({ content: '설정된 서버에서만 사용할 수 있습니다.', flags: MessageFlags.Ephemeral });
    return '';
  }
  await interaction.deferReply({ flags: MessageFlags.Ephemeral });
  const guild = interaction.guild;
  const [actor, bot] = await Promise.all([guild.members.fetch(interaction.user.id), guild.members.fetchMe()]);
  const user = interaction.options.getUser('member');
  const target = user ? await guild.members.fetch(user.id) : null;
  const command = interaction.commandName;
  if (!commandAllowed({ command, guildId: guild.id, config, actor, bot, target, channel: interaction.channel })) {
    await interaction.editReply('권한 또는 역할 서열 때문에 실행할 수 없습니다.');
    return '';
  }
  const reason = `${interaction.user.id}: ${interaction.options.getString('reason') ?? '서버 관리'}`;
  let result: string;
  switch (command) {
    case 'purge': {
      if (!('bulkDelete' in interaction.channel)) {
        await interaction.editReply('메시지 정리가 가능한 채널에서 실행해 주세요.');
        return '';
      }
      const deleted = await interaction.channel.bulkDelete(interaction.options.getInteger('count', true), true);
      result = `${deleted.size}개 메시지를 삭제했습니다. (14일 이상 된 메시지는 제외)`;
      break;
    }
    case 'roles': {
      const messageId = interaction.options.getString('message', true);
      const bindings = config.reactionRoles.filter(binding => binding.messageId === messageId);
      if (bindings.length === 0) {
        await interaction.editReply('config.json의 reactionRoles에 먼저 메시지를 등록하고 재시작해 주세요.');
        return '';
      }
      for (const binding of bindings) {
        const role = await guild.roles.fetch(binding.roleId);
        if (!role || !reactionRole({ config, binding, bot, role, guildId: guild.id, channelId: binding.channelId, messageId, emoji: binding.emoji, memberBot: false })) {
          await interaction.editReply('안전하지 않거나 봇보다 높은 역할이 포함되어 있습니다.');
          return '';
        }
      }
      for (const binding of bindings) {
        const channel = await guild.channels.fetch(binding.channelId);
        if (!channel?.isTextBased()) {
          await interaction.editReply('역할 메시지 채널을 찾을 수 없습니다.');
          return '';
        }
        const message = await channel.messages.fetch(messageId);
        await message.react(binding.emoji);
      }
      result = '반응 역할 메시지를 준비했습니다. 반응을 추가하면 역할이 붙고 제거하면 역할이 해제됩니다.';
      break;
    }
    case 'timeout':
      if (!target) return '';
      await target.timeout(interaction.options.getInteger('minutes', true) * 60_000, reason);
      result = '멤버를 일시 제한했습니다.';
      break;
    case 'kick':
      if (!target) return '';
      await target.kick(reason);
      result = '멤버를 추방했습니다.';
      break;
    case 'ban':
      if (!target) return '';
      await target.ban({ reason });
      result = '멤버를 차단했습니다.';
      break;
    default:
      await interaction.editReply('지원하지 않는 명령입니다.');
      return '';
  }
  await interaction.editReply(result);
  return `${command} actor=${actor.id} target=${target?.id ?? interaction.channelId}`;
}
