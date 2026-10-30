import { randomUUID } from 'node:crypto';
import { Arena } from './json';
import type { Environment } from './index';

export class Web {
  private readonly url: URL;
  private readonly headers = new Headers({ 'Content-Type': 'application/json; charset=utf-8', 'X-Content-Type-Options': 'nosniff' });
  private status = 500;
  private output = '{"error":"internal"}';
  constructor(private readonly request: Request, private readonly body: string, private readonly context: { readonly env: Environment; readonly arena: Arena }) {
    this.url = new URL(request.url);
  }
  response(): Response { return new Response(this.status === 204 ? null : this.output, { status: this.status, headers: this.headers }); }
  readonly native = {
    fw_env: (name: string, fallback: string) => { const value: unknown = Reflect.get(this.context.env, name); return typeof value === 'string' ? value : fallback; },
    fw_method: (_request: bigint) => this.request.method,
    fw_path: (_request: bigint) => this.url.pathname,
    fw_header: (_request: bigint, name: string) => this.request.headers.get(name) ?? '',
    fw_query: (_request: bigint, name: string) => this.url.searchParams.get(name) ?? '',
    fw_body: (_request: bigint) => this.body,
    fw_segment: (_request: bigint, index: bigint) => this.url.pathname.replace(/^\/+/, '').split('/')[Number(index)] ?? '',
    fw_segments: (_request: bigint) => {
      const path = this.url.pathname.replace(/^\//, '');
      return path ? BigInt(path.split('/').length) : 0n;
    },
    fw_respond: (_request: bigint, status: bigint, body: string) => { this.status = Number(status); this.output = body; return 1n; },
    fw_redirect: (_request: bigint, url: string) => {
      this.status = 302; this.output = ''; this.headers.set('Location', url); this.headers.set('Cache-Control', 'no-store'); return 1n;
    },
    fw_cors: (_request: bigint, origins: string) => {
      const origin = this.request.headers.get('Origin');
      if (!origin || !origins.split(',').map(value => value.trim()).includes(origin)) return 0n;
      this.headers.set('Access-Control-Allow-Origin', origin); this.headers.set('Vary', 'Origin');
      this.headers.set('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
      this.headers.set('Access-Control-Allow-Headers', 'authorization, content-type'); this.headers.set('Access-Control-Max-Age', '3600'); return 1n;
    },
    fw_cookie: (_request: bigint, name: string) => {
      for (const item of (this.request.headers.get('Cookie') ?? '').split(';')) {
        const [key, ...value] = item.trim().split('=');
        if (key === name) return value.join('=');
      }
      return '';
    },
    fw_set_cookie: (_request: bigint, name: string, value: string, age: bigint, secure: bigint) => {
      this.headers.append('Set-Cookie', `${name}=${value}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${age}${secure ? '; Secure' : ''}`); return 1n;
    },
    fw_trim: (value: string) => value.trim(),
    fw_chars: (value: string) => BigInt([...value].length),
    fw_valid: (value: string, kind: string) => {
      switch (kind) {
        case 'login': return /^[a-zA-Z0-9-]{1,39}$/.test(value) ? 1n : 0n;
        case 'return_path': return /^\/(?!\/)[a-zA-Z0-9/_.~-]*$/.test(value) ? 1n : 0n;
        default: throw new RangeError(`Unsupported Forge validation primitive: ${kind}`);
      }
    },
    fw_lower: (value: string) => value.toLowerCase(),
    fw_equal: (left: string, right: string) => left === right ? 1n : 0n,
    fw_starts: (value: string, prefix: string) => value.startsWith(prefix) ? 1n : 0n,
    fw_uuid: () => randomUUID(),
    fw_now: () => BigInt(Math.floor(Date.now() / 1000)),
    fw_urlencode: (value: string) => encodeURIComponent(value),
    fw_rate: async (key: string, maximum: bigint, seconds: bigint) => {
      const stub = this.context.env.RATE_LIMITS.get(this.context.env.RATE_LIMITS.idFromName(key));
      const response = await stub.fetch('https://rate.internal/', { method: 'POST', body: JSON.stringify({ maximum: Number(maximum), seconds: Number(seconds) }) });
      if (!response.ok) throw new Error('Forge rate storage unavailable');
      return await response.text() === '1' ? 1n : 0n;
    },
    fw_fetch: async (url: string, method: string, body: string, bearer: string) => {
      const headers = new Headers({ Accept: 'application/json', 'User-Agent': 'forge-registry-worker' });
      if (bearer) headers.set('Authorization', `Bearer ${bearer}`);
      if (body) headers.set('Content-Type', 'application/x-www-form-urlencoded');
      // Protocol primitive: OAuth policy and endpoints remain in auth.fg. Never retry token exchanges.
      const response = await fetch(url, { method, headers, ...(body ? { body } : {}), redirect: 'manual', signal: AbortSignal.timeout(15000) });
      return response.ok ? this.context.arena.parse(await response.text()) : 0n;
    },
  };
}
