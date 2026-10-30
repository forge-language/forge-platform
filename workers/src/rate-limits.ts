import { DurableObject } from 'cloudflare:workers';
import { z } from 'zod';

const requestSchema = z.object({ maximum: z.number().int().positive().max(1000000), seconds: z.number().int().positive().max(86400) });
const bucketSchema = z.object({ start: z.number(), count: z.number().int().nonnegative() });

// One object per opaque key. This is a generic atomic resource primitive; callers supply policy.
export class RateLimits extends DurableObject {
  async fetch(request: Request): Promise<Response> {
    const input = requestSchema.parse(await request.json());
    const now = Math.floor(Date.now() / 1000);
    const allowed = await this.ctx.storage.transaction(async storage => {
      const previous: unknown = await storage.get('bucket');
      let bucket = previous === undefined ? { start: now, count: 0 } : bucketSchema.parse(previous);
      if (now - bucket.start >= input.seconds) bucket = { start: now, count: 0 };
      if (bucket.count >= input.maximum) return false;
      await storage.put('bucket', { start: bucket.start, count: bucket.count + 1 });
      await storage.setAlarm((bucket.start + input.seconds) * 1000);
      return true;
    });
    return new Response(allowed ? '1' : '0');
  }
  async alarm(): Promise<void> { await this.ctx.storage.deleteAll(); }
}
