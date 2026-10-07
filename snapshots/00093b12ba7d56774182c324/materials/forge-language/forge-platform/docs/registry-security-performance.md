# Registry security and performance

Measured 2026-10-02T04:53:44Z; 3 alternating before/after repeats of 4 seconds after 1 second warmup. Values are medians.

| API | Clients | Before req/s | After req/s | Change | Before p95 ms | After p95 ms | Errors |
|---|---:|---:|---:|---:|---:|---:|---:|
| /api/health | 1 | 3161.65 | 3025.59 | -4.30% | 0.499 | 0.539 | 0 |
| /api/health | 16 | 7787.38 | 7781.01 | -0.08% | 2.457 | 2.767 | 0 |
| /api/health | 64 | 7639.18 | 7461.80 | -2.32% | 9.639 | 10.791 | 0 |
| /api/packages | 1 | 513.46 | 556.30 | +8.34% | 3.195 | 2.964 | 0 |
| /api/packages | 16 | 4209.67 | 2771.71 | -34.16% | 6.736 | 10.478 | 0 |
| /api/packages | 64 | 4326.44 | 4775.48 | +10.38% | 26.938 | 31.535 | 0 |
| /api/packages/forge-web | 1 | 600.47 | 778.41 | +29.63% | 2.890 | 2.302 | 0 |
| /api/packages/forge-web | 16 | 4764.82 | 4750.61 | -0.30% | 5.078 | 6.370 | 0 |
| /api/packages/forge-web | 64 | 5045.76 | 5877.71 | +16.49% | 15.747 | 16.608 | 0 |

2 CPUs / 512 MiB per API, five PostgreSQL leases, eight HTTP workers. Image IDs, raw trials, CPU and memory are in JSON. Responses match except publish timestamps.

The after variant removes database leasing from stateless routes and caches prepared SQL plans. Both variants read registry data and ownership from PostgreSQL on every request. The after variant also includes stricter JSON/JWT parsing and explicit proxy trust. This combined comparison does not isolate each change.

This shared host still runs production applications. Small differences can be host variation. Python aiohttp may limit high-throughput endpoints. This is a registry application comparison, not a Rust/Forge language claim. Existing published SDK binaries and production deployments are not updated by this benchmark.
