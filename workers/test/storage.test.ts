import { createHash } from 'node:crypto';
import { afterEach, expect, it, vi } from 'vitest';
import { Arena } from '../src/json';
import { storageNative } from '../src/storage';

afterEach(() => vi.unstubAllGlobals());

it('streams an immutable archive to its content digest URL without sending storage credentials to the source', async () => {
  const arena = new Arena();
  const source = 'https://codeload.github.com/TestUser/test/tar.gz/' + 'a'.repeat(40);
  const data = 'archive-content';
  const digest = createHash('sha256').update(data).digest('hex');
  const requests: string[] = [];
  vi.stubGlobal('fetch', async (url: string, init?: RequestInit) => {
    requests.push(url);
    if (url === source) {
      expect(init?.headers).toBeUndefined();
      return new Response(data);
    }
    expect(url).toBe(`https://storage.example/v1/objects/${digest}`);
    expect(init?.headers).toEqual({ Authorization: 'Bearer storage-secret', 'Content-Type': 'application/octet-stream' });
    expect(await new Response(init?.body).text()).toBe(data);
    return Response.json({ sha256: digest, size: data.length }, { status: 201 });
  });
  const result = await storageNative(arena).fs_fetch_archive(source, 'https://storage.example', 'storage-secret');
  expect(arena.value(result)).toEqual({ sha256: digest, size: data.length });
  expect(requests).toEqual([source, source, `https://storage.example/v1/objects/${digest}`]);
});

it('fails closed when downloading an archive fails', async () => {
  const arena = new Arena();
  const transport = vi.fn().mockResolvedValue(new Response('', { status: 503 }));
  vi.stubGlobal('fetch', transport);
  expect(await storageNative(arena).fs_fetch_archive('https://codeload.github.com/Test/test/tar.gz/abc', 'https://storage.example', 'secret')).toBe(0n);
  expect(transport).toHaveBeenCalledTimes(1);
});
