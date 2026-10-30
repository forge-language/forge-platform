# Worker validation

Recorded locally under `.omo/evidence/workers/` from the repository root:

| Scenario | Invocation | Observable | Artifact |
| --- | --- | --- | --- |
| Forge compilation, strict TypeScript and JSON/crypto primitive unit cases | `npx --yes --package=node@22 -c 'npm test --prefix workers'` | 2 files, 6 tests passed | `unit.log` |
| Workers bundle without deploying | `npx --yes --package=node@22 -c 'npm run check --prefix workers'` | Wrangler 4.156.0 bundle succeeds, dry-run exits without deployment | `check.log` |
| Dependency audit | `npx --yes --package=node@22 -c 'npm audit --prefix workers'` | 0 vulnerabilities | `check.log` |
| Real local Workers runtime and isolated PostgreSQL | `npx --yes --package=node@22 -c 'npm run test:integration --prefix workers'` | 18 registry tests pass; 32 overlapping identities stay isolated; CORS and Durable Object counter pass; oversized body returns 413; three requests with database down return 503 | `integration.log`, `registry.log`, `surface.log`, `worker.log` |

Initial validation exposed a typed Blob chunk mismatch and a database readiness race against PostgreSQL's temporary initialization server. Chunk copies now have an explicit ArrayBuffer-backed type, and readiness probes use TCP. Initial dependency audit findings were resolved by updating the pinned Vitest and Zod versions. The integration image uses the repository's available PostgreSQL 16 image. The full relevant scenarios above passed after those fixes.

No production stack, Cloudflare account, route, DNS entry, OAuth application or database was changed. Configuration and live-service limits are documented in `docs/cloudflare-workers.md`.
