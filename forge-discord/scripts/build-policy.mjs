import { execFileSync } from 'node:child_process';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { homedir } from 'node:os';
import { resolve } from 'node:path';

const root = new URL('../', import.meta.url);
await mkdir(new URL('dist', root), { recursive: true });
const output = new URL('dist/policy.js', root);
execFileSync(process.env.FORGE ?? resolve(homedir(), '.forge/bin/forge'), [
  new URL('src/policy.fg', root).pathname, '--emit-js', '-o', output.pathname,
], { stdio: 'inherit' });
const compiled = await readFile(output, 'utf8');
await writeFile(output, `${compiled}\nexport { fr_authorize as authorize, fr_reaction_role as reaction_role, fr_automod as automod };\n`);
