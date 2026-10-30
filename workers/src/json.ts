import { z } from 'zod';

export type Json = z.infer<ReturnType<typeof z.json>>;
function unambiguous(text: string): boolean {
  const tokens = text.match(/"(?:[^"\\]|\\.)*"|[{}\[\],:]|[^\s{}\[\],:]+/g) ?? [];
  let index = 0;
  const value = (depth: number): boolean => {
    if (depth >= 32) return false;
    const token = tokens[index++];
    if (token === '{') {
      const keys = new Set<string>();
      if (tokens[index] === '}') { index++; return true; }
      do {
        const key: unknown = JSON.parse(tokens[index++] ?? 'null');
        if (typeof key !== 'string' || key.includes('\0') || keys.has(key) || tokens[index++] !== ':') return false;
        keys.add(key);
        if (!value(depth + 1)) return false;
        if (tokens[index] === '}') { index++; return true; }
      } while (tokens[index++] === ',');
      return false;
    }
    if (token === '[') {
      if (tokens[index] === ']') { index++; return true; }
      do {
        if (!value(depth + 1)) return false;
        if (tokens[index] === ']') { index++; return true; }
      } while (tokens[index++] === ',');
      return false;
    }
    const primitive: unknown = JSON.parse(token ?? '');
    return typeof primitive !== 'object' || primitive === null
      ? typeof primitive !== 'string' || !primitive.includes('\0') : false;
  };
  return value(0) && index === tokens.length;
}
export class Arena {
  private readonly values = new Map<bigint, Json>();
  private next = 1n;
  store(value: Json): bigint {
    if (value === null) return 0n;
    const handle = this.next++;
    this.values.set(handle, value);
    return handle;
  }
  value(handle: bigint): Json { return this.values.get(handle) ?? null; }
  object(handle: bigint): Record<string, Json> | null {
    const value = this.value(handle);
    return value !== null && typeof value === 'object' && !Array.isArray(value) ? value : null;
  }
  text(handle: bigint): string {
    const value = this.value(handle);
    return typeof value === 'string' ? value : '';
  }
  parse(text: string): bigint {
    try {
      if (new TextEncoder().encode(text).length > 4194304 || !unambiguous(text)) return 0n;
      return this.store(z.json().parse(JSON.parse(text)));
    }
    catch (error) {
      if (error instanceof SyntaxError || error instanceof z.ZodError) return 0n;
      throw error;
    }
  }
  readonly native = {
    fw_has: (handle: bigint, key: string) => Object.hasOwn(this.object(handle) ?? {}, key) ? 1n : 0n,
    fw_parse: (text: string) => this.parse(text),
    fw_kind: (handle: bigint) => {
      const value = this.value(handle);
      if (value === null) return 0n;
      if (typeof value === 'boolean') return 1n;
      if (typeof value === 'number') return 2n;
      if (typeof value === 'string') return 3n;
      return Array.isArray(value) ? 5n : 4n;
    },
    fw_get: (handle: bigint, key: string) => this.store(this.object(handle)?.[key] ?? null),
    fw_at: (handle: bigint, index: bigint) => {
      const value = this.value(handle);
      return this.store(Array.isArray(value) ? value[Number(index)] ?? null : null);
    },
    fw_count: (handle: bigint) => {
      const value = this.value(handle);
      return BigInt(value !== null && typeof value === 'object' ? Object.keys(value).length : 0);
    },
    fw_text: (handle: bigint) => this.text(handle),
    fw_integer: (handle: bigint) => { const value = this.value(handle); return typeof value === 'number' ? BigInt(Math.trunc(value)) : 0n; },
    fw_boolean: (handle: bigint) => this.value(handle) === true ? 1n : 0n,
    fw_dump: (handle: bigint) => JSON.stringify(this.value(handle)),
    fw_object: () => this.store({}),
    fw_array: () => this.store([]),
    fw_string: (value: string) => this.store(value),
    fw_number: (value: bigint) => this.store(Number(value)),
    fw_bool: (value: bigint) => this.store(value !== 0n),
    fw_set: (handle: bigint, key: string, child: bigint) => {
      const value = this.object(handle);
      if (!value) return 0n;
      Object.defineProperty(value, key, { value: this.value(child), enumerable: true, writable: true, configurable: true });
      return 1n;
    },
    fw_push: (handle: bigint, child: bigint) => {
      const value = this.value(handle);
      if (!Array.isArray(value)) return 0n;
      value.push(this.value(child)); return 1n;
    },
    fw_keys: (handle: bigint) => this.store(Object.keys(this.object(handle) ?? {})),
  };
}
