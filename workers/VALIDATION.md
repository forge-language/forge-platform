# Worker validation

Recorded locally under `.omo/evidence/workers/` from the repository root:

| Scenario | Invocation | Observable | Artifact |
| --- | --- | --- | --- |
| Forge compilation, strict TypeScript and JSON/crypto/artifact protocol unit cases after upstream merge | `npx --yes --package=node@22 -c 'npm test --prefix workers'` | 3 files, 9 tests passed | `merge-final.log` |
| Workers bundle without deploying | `npx --yes --package=node@22 -c 'npm run check --prefix workers'` | Wrangler 4.156.0 bundle succeeds, dry-run exits without deployment | `check.log` |
| Dependency audit | `npx --yes --package=node@22 -c 'npm audit --prefix workers'` | 0 vulnerabilities | `check.log` |
| Real local Workers runtime, upstream repository checker fixture and isolated PostgreSQL with every migration | `npx --yes --package=node@22 -c 'npm run test:integration --prefix workers'` | 25 registry tests pass; 32 overlapping identities stay isolated; CORS and Durable Object counter pass; oversized body returns 413; three `/api/stats` requests with database down return 503 | `merge-final.log`, `registry.log`, `surface.log`, `worker.log` |

Initial validation exposed a typed Blob chunk mismatch and a database readiness race against PostgreSQL's temporary initialization server. Chunk copies now have an explicit ArrayBuffer-backed type, and readiness probes use TCP. Initial dependency audit findings were resolved by updating the pinned Vitest and Zod versions. The integration image uses the repository's available PostgreSQL 16 image. The full relevant scenarios above passed after those fixes.

The upstream merge introduced `fpg_query_prepared`, repository-checker HTTP calls, GitHub ID authorization, artifact transport and optional Redis cache hooks. The first merged integration run failed publishing because the new PostgreSQL extern was missing; the adapter now implements it and runs all migrations. JSON ambiguity and unsafe signed claims are rejected. The checker fixture uses its existing inspection implementation and an isolated loopback port. Artifact protocol tests mock codeload and the storage service, verify the digest URL and streamed bytes, prevent source credential leakage and reject download failures. Live artifact delivery is not covered. Redis remains unconfigured and reads go directly to PostgreSQL; Hyperdrive query caching must also be disabled as documented.

No production stack, Cloudflare account, route, DNS entry, OAuth application or database was changed. Configuration and live-service limits are documented in `docs/cloudflare-workers.md`.
