import { loadConfig } from './config.js';
const config = loadConfig();
console.log(JSON.stringify({ valid: true, guildId: config.guildId, reactionBindings: config.reactionRoles.length }));
