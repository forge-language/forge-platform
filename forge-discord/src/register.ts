import { REST, Routes } from 'discord.js';
import { commands } from './commands.js';
import { credentials, loadConfig } from './config.js';

const config = loadConfig();
const environment = credentials();
const rest = new REST({ version: '10' }).setToken(environment.DISCORD_TOKEN);
await rest.put(Routes.applicationGuildCommands(environment.DISCORD_CLIENT_ID, config.guildId), { body: commands });
console.log(JSON.stringify({ event: 'commands.registered', guildId: config.guildId, count: commands.length }));
