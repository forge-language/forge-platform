# Redis cache measurement — 2026-10-06

Recorded at `2026-10-06T12:39:04Z` on an isolated local Compose fixture. The same Forge API image was tested first with `REDIS_HOST` empty, then with a warmed Redis cache.

| Variant | Concurrent clients | Samples | Median (ms) | p95 (ms) | Redis hits |
| --- | ---: | ---: | ---: | ---: | ---: |
| postgres-only | 1 | 100 | 1.8402 | 2.8898 | 0 |
| postgres-only | 8 | 100 | 4.3776 | 8.5818 | 0 |
| redis-warm | 1 | 100 | 1.6884 | 2.2904 | 200 |
| redis-warm | 8 | 100 | 4.0158 | 6.9540 | 200 |

Each variant has five initial warmup requests and 100 measured requests at each concurrency level. The public `/api/packages` response contains 3 seeded packages. The client opens a fresh HTTP connection for every request. Redis hit counters confirm actual cache access; two hits per request correspond to generation lookup and payload lookup. All measured payloads matched the PostgreSQL baseline.

These are local latency measurements, including HTTP connection setup and Python scheduling. They exclude Nginx, TLS and Cloudflare. This small catalog and one ordered run do not establish production throughput or a universal speedup; Redis avoids repeated database reads but adds connection and protocol overhead.

Cache policy: public registry GETs only, 30-second TTL, 256 KiB maximum body, UUID generation rotation after successful publish. Late fills remain in the previous namespace. Redis failures fall back to PostgreSQL; if publish invalidation fails, existing entries can remain stale until their TTL expires.

Raw samples and image identity: [`benchmarks/redis-cache-2026-10-06.json`](../benchmarks/redis-cache-2026-10-06.json). Reproduce with:

```sh
python3 benchmarks/redis-cache.py --verify
```
