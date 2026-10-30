import { Client } from 'pg';
import { createProgram } from '../generated/program.js';
import { Arena } from './json';
import { Postgres } from './postgres';
import { Web } from './web';
import { cryptoNative } from './crypto';
import { storageNative } from './storage';
export { RateLimits } from './rate-limits';

export type Environment = {
  readonly HYPERDRIVE: { readonly connectionString: string };
  readonly RATE_LIMITS: DurableObjectNamespace;
  readonly JWT_SECRET: string;
  readonly FRONTEND_URL?: string;
  readonly BACKEND_BASE_URL?: string;
  readonly CORS_ALLOWED_ORIGINS?: string;
  readonly GITHUB_CLIENT_ID?: string;
  readonly GITHUB_CLIENT_SECRET?: string;
  readonly ADMIN_GITHUB_ID?: string;
  readonly REPOSITORY_CHECK_URL?: string;
  readonly REPOSITORY_CHECK_TOKEN?: string;
  readonly STORAGE_BASE_URL?: string;
  readonly STORAGE_PUBLIC_URL?: string;
  readonly STORAGE_TOKEN?: string;
};

export async function handle(request: Request, env: Environment): Promise<Response> {
  if (!env.JWT_SECRET || !env.HYPERDRIVE?.connectionString) return Response.json({ error: 'unavailable' }, { status: 503 });
  const reader = request.body?.getReader();
  const chunks: Uint8Array<ArrayBuffer>[] = [];
  let size = 0;
  if (reader) {
    for (;;) {
      const part = await reader.read();
      if (part.done) break;
      size += part.value.byteLength;
      if (size > 2097152) {
        await reader.cancel();
        return Response.json({ error: 'validation', message: 'payload too large' }, { status: 413 });
      }
      chunks.push(new Uint8Array(part.value));
    }
  }
  const body = await new Blob(chunks).text();
  const arena = new Arena();
  const database = new Postgres(new Client({ connectionString: env.HYPERDRIVE.connectionString, connectionTimeoutMillis: 5000, query_timeout: 15000 }));
  const web = new Web(request, body, { env, arena });
  try {
    const program = createProgram({ ...arena.native, ...database.native, ...web.native, ...cryptoNative(arena), ...storageNative(arena),
      platform_redis_close: (client: bigint) => { if (client !== 0n) throw new Error('Redis is not configured for Workers'); return 1n; },
      platform_redis_open: () => { throw new Error('Redis TCP caching is not supported by this Worker adapter; leave REDIS_HOST unset'); },
    });
    await program.dispatch(1n);
    return web.response();
  } catch (error) {
    if (!(error instanceof Error)) throw error;
    console.error(JSON.stringify({ event: 'worker.request.failed' }));
    web.native.fw_respond(1n, 503n, '{"error":"unavailable"}');
    return web.response();
  } finally {
    try { await database.close(); }
    catch (error) {
      if (!(error instanceof Error)) throw error;
      console.error(JSON.stringify({ event: 'postgres.close.failed' }));
    }
  }
}

// Cloudflare's module Worker entrypoint requires a default export.
export default { fetch: handle };
