import { createHash } from 'node:crypto';
import { Arena } from './json';

const limit = 256 * 1024 * 1024;
export function storageNative(arena: Arena) {
  return {
    fs_fetch_archive: async (url: string, base: string, token: string) => {
      if (!url.startsWith('https://codeload.github.com/')) return 0n;
      try {
        const first = await fetch(url, { redirect: 'manual', signal: AbortSignal.timeout(180000) });
        if (first.status !== 200 || !first.body) return 0n;
        const hash = createHash('sha256');
        let size = 0;
        for await (const chunk of first.body) {
          size += chunk.byteLength;
          if (size > limit) { await first.body.cancel(); return 0n; }
          hash.update(chunk);
        }
        const digest = hash.digest('hex');
        // Immutable commit archives are read twice to avoid buffering up to 256 MiB in an isolate.
        // The storage service authenticates the PUT and verifies its content against this digest.
        const second = await fetch(url, { redirect: 'manual', signal: AbortSignal.timeout(180000) });
        if (second.status !== 200 || !second.body) return 0n;
        let uploadSize = 0;
        const bounded = second.body.pipeThrough(new TransformStream<Uint8Array, Uint8Array>({ transform(chunk, controller) {
          uploadSize += chunk.byteLength;
          if (uploadSize > limit) throw new RangeError('Archive exceeds Forge storage limit');
          controller.enqueue(chunk);
        } }));
        const response = await fetch(`${base}/v1/objects/${digest}`, { method: 'PUT', headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/octet-stream' }, body: bounded, redirect: 'manual', signal: AbortSignal.timeout(180000) });
        return response.status === 200 || response.status === 201 ? arena.parse(await response.text()) : 0n;
      } catch (error) {
        if (!(error instanceof Error)) throw error;
        return 0n;
      }
    },
  };
}
