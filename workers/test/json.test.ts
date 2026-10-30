import { expect, it } from 'vitest';
import { Arena } from '../src/json';

it('keeps a JSON object key when its name matches an object prototype property', () => {
  const arena = new Arena();
  const handle = arena.native.fw_object();
  arena.native.fw_set(handle, '__proto__', arena.store('ordinary-key'));
  expect(arena.native.fw_dump(handle)).toBe('{"__proto__":"ordinary-key"}');
});

it('keeps handles isolated when different requests allocate the same handle number', () => {
  const first = new Arena();
  const second = new Arena();
  const handle = first.store({ sub: 'first' });
  second.store({ sub: 'second' });
  expect(first.value(handle)).toEqual({ sub: 'first' });
});

it('rejects duplicate decoded object keys and NUL strings from untrusted JSON', () => {
  const arena = new Arena();
  for (const value of ['{"name":1,"name":2}', '{"name":1,"\\u006eame":2}', '{"name":"unsafe\\u0000suffix"}']) {
    expect(arena.parse(value)).toBe(0n);
  }
  expect(arena.value(arena.parse('{"outer":{"name":"safe"},"name":"also safe"}'))).toEqual({ outer: { name: 'safe' }, name: 'also safe' });
});
