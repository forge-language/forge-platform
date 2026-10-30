import { describe, expect, it } from 'vitest';
import { Arena } from '../src/json';
import { cryptoNative } from '../src/crypto';

describe('native JWT protocol', () => {
  it('verifies signed claims when the key matches', () => {
    const arena = new Arena();
    const native = cryptoNative(arena);
    const claims = { sub: 'ForgeUser', role: 'user', exp: Math.floor(Date.now() / 1000) + 600 };
    const token = native.fw_jwt_sign(arena.store(claims), 'integration-key');
    expect(arena.value(native.fw_jwt_verify(token, 'integration-key'))).toEqual(claims);
  });
  it('rejects claims when another request uses a different key', () => {
    const first = new Arena();
    const second = new Arena();
    const token = cryptoNative(first).fw_jwt_sign(first.store({ sub: 'First', role: 'admin', exp: 9999999999 }), 'first-key');
    expect(cryptoNative(second).fw_jwt_verify(token, 'second-key')).toBe(0n);
  });
  it('rejects a token when its signature was changed', () => {
    const arena = new Arena();
    const native = cryptoNative(arena);
    const token = native.fw_jwt_sign(arena.store({ sub: 'First', role: 'admin', exp: 9999999999 }), 'key');
    const changed = `${token.slice(0, -8)}AAAAAAAA`;
    expect(native.fw_jwt_verify(changed, 'key')).toBe(0n);
  });
  it('rejects claims when their expiry has passed', () => {
    const arena = new Arena();
    const native = cryptoNative(arena);
    const token = native.fw_jwt_sign(arena.store({ sub: 'First', role: 'admin', exp: 1 }), 'key');
    expect(native.fw_jwt_verify(token, 'key')).toBe(0n);
  });
});
