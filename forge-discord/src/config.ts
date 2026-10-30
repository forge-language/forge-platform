import { readFileSync } from 'node:fs';
import { z } from 'zod';

const snowflake = z.string().regex(/^[1-9][0-9]{16,19}$/);
const emoji = z.string().min(1).max(100).refine(value => !value.includes('\n'));
const binding = z.object({
  channelId: snowflake, messageId: snowflake, emoji,
  roleId: snowflake,
}).strict().readonly();
export const configSchema = z.object({
  guildId: snowflake,
  logChannelId: snowflake.optional(),
  reactionRoles: z.array(binding).max(100).default([]),
  moderation: z.object({
    enabled: z.boolean().default(true),
    blockedTerms: z.array(z.string().trim().min(1).max(100).refine(term => !term.includes('\n'))).max(500),
    exemptRoleIds: z.array(snowflake).default([]),
    exemptChannelIds: z.array(snowflake).default([]),
    messageLimit: z.number().int().min(2).max(100).default(6),
    windowSeconds: z.number().int().min(1).max(60).default(5),
    mentionLimit: z.number().int().min(2).max(100).default(8),
    timeoutSeconds: z.number().int().min(0).max(2419200).default(300),
  }).strict().readonly(),
}).strict().superRefine((config, context) => {
  const keys = new Set<string>();
  const roles = new Set<string>();
  config.reactionRoles.forEach((entry, index) => {
    const key = `${entry.channelId}/${entry.messageId}/${entry.emoji}`;
    if (keys.has(key)) context.addIssue({ code: 'custom', path: ['reactionRoles', index], message: 'Duplicate reaction binding' });
    if (roles.has(entry.roleId)) context.addIssue({ code: 'custom', path: ['reactionRoles', index], message: 'Each role must have one reaction binding' });
    keys.add(key);
    roles.add(entry.roleId);
  });
}).readonly();
export type Config = z.infer<typeof configSchema>;
export function loadConfig(): Config {
  const raw: unknown = JSON.parse(readFileSync(process.env['BOT_CONFIG'] ?? 'config.json', 'utf8'));
  return configSchema.parse(raw);
}
export function credentials() {
  return z.object({ DISCORD_TOKEN: z.string().min(1), DISCORD_CLIENT_ID: snowflake })
    .parse(process.env);
}
export function normalize(value: string): string { return value.normalize('NFKC').toLocaleLowerCase('en-US'); }
