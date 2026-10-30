import { createHmac, timingSafeEqual } from 'node:crypto';
import { Arena } from './json';

export function cryptoNative(arena: Arena) {
  return {
    fw_jwt_sign: (handle: bigint, secret: string) => {
      if (!arena.object(handle) || !secret) return '';
      const payload = Buffer.from(JSON.stringify(arena.value(handle))).toString('base64url');
      const message = `eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.${payload}`;
      return `${message}.${createHmac('sha256', secret).update(message).digest('base64url')}`;
    },
    fw_jwt_verify: (token: string, secret: string) => {
      if (!secret || token.length > 16384) return 0n;
      const parts = token.split('.');
      const [header, payload, signature] = parts;
      if (parts.length !== 3 || !header || !payload || !signature) return 0n;
      const parsed = arena.parse(Buffer.from(header, 'base64url').toString('utf8'));
      const metadata = arena.object(parsed);
      if (metadata?.['alg'] !== 'HS256' || Object.hasOwn(metadata, 'crit') || Object.hasOwn(metadata, 'b64')) return 0n;
      const expected = createHmac('sha256', secret).update(`${header}.${payload}`).digest();
      const supplied = Buffer.from(signature, 'base64url');
      if (expected.length !== supplied.length || !timingSafeEqual(expected, supplied)) return 0n;
      const claims = arena.parse(Buffer.from(payload, 'base64url').toString('utf8'));
      const value = arena.object(claims);
      if (!value || typeof value['exp'] !== 'number' || !Number.isSafeInteger(value['exp']) || value['exp'] <= Date.now() / 1000 ||
        (Object.hasOwn(value, 'nbf') && (typeof value['nbf'] !== 'number' || !Number.isSafeInteger(value['nbf']) || value['nbf'] > Date.now() / 1000)) ||
        typeof value['sub'] !== 'string' || !value['sub'] || typeof value['role'] !== 'string') return 0n;
      return claims;
    },
  };
}
