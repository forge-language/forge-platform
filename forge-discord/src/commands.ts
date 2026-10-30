import { PermissionFlagsBits, SlashCommandBuilder } from 'discord.js';

export const commands = [
  new SlashCommandBuilder().setName('purge').setDescription('최근 메시지를 정리합니다 (14일 미만).')
    .setDefaultMemberPermissions(PermissionFlagsBits.ManageMessages)
    .addIntegerOption(option => option.setName('count').setDescription('삭제 개수').setRequired(true).setMinValue(1).setMaxValue(100)),
  new SlashCommandBuilder().setName('timeout').setDescription('멤버를 일시 제한합니다.')
    .setDefaultMemberPermissions(PermissionFlagsBits.ModerateMembers)
    .addUserOption(option => option.setName('member').setDescription('대상 멤버').setRequired(true))
    .addIntegerOption(option => option.setName('minutes').setDescription('제한 시간 (분)').setRequired(true).setMinValue(1).setMaxValue(40320))
    .addStringOption(option => option.setName('reason').setDescription('사유').setMaxLength(400)),
  ...['kick', 'ban'].map(name => new SlashCommandBuilder().setName(name).setDescription(name === 'kick' ? '멤버를 추방합니다.' : '멤버를 차단합니다.')
    .setDefaultMemberPermissions(name === 'kick' ? PermissionFlagsBits.KickMembers : PermissionFlagsBits.BanMembers)
    .addUserOption(option => option.setName('member').setDescription('대상 멤버').setRequired(true))
    .addStringOption(option => option.setName('reason').setDescription('사유').setMaxLength(400))),
  new SlashCommandBuilder().setName('roles').setDescription('설정된 역할 메시지에 반응 이모지를 붙입니다.')
    .setDefaultMemberPermissions(PermissionFlagsBits.ManageGuild)
    .addStringOption(option => option.setName('message').setDescription('config.json에 등록한 메시지 ID').setRequired(true)),
].map(command => command.setDMPermission(false).toJSON());
