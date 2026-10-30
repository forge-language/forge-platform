# Cloudflare Workers backend

`workers/` prepares the registry API for Workers. The original native backend and Docker stack remain supported. Nothing is deployed and no DNS route is changed by building or testing this adapter.

Routes, authorization, SQL, manifests and package resolution still come from `backend/src/*.fg`. The build uses the installed Forge compiler's JavaScript output, then a TypeScript AST transformation makes generated function calls asynchronous. Each request creates its own compiled program, JSON arena and PostgreSQL client; the bridge never assigns `globalThis.ForgeNative`. Changes to the Forge compiler output must pass the integration suite before upgrading the compiler.

The TypeScript adapter supplies HTTP, JSON, HMAC/JWT protocol, PostgreSQL, artifact transport and resource lifetime primitives. Rate policy remains in Forge; a Durable Object supplies atomic counters across Worker instances. The database remains PostgreSQL through Hyperdrive, with all existing `backend/migrations/*.sql` migrations, including immutable numeric GitHub owner IDs. This is not a D1 conversion. Optional native Redis caching is disabled for Workers: leave `REDIS_HOST` unset; registry reads go directly to PostgreSQL. The Durable Object rate counter remains enabled independently of Redis.

## Local validation

Requirements: Node.js 22 or newer, npm, Docker, Python 3 and Forge installed at `~/.forge/bin/forge` (or set `FORGE` to its executable). Initialize the repository's vendor submodules before building.

```sh
cd workers
npm ci
npm test
npm run check
npm run test:integration
```

`check` bundles with `wrangler deploy --dry-run`; it does not deploy. Integration starts a uniquely named PostgreSQL 16 container on an ephemeral loopback port, applies every registry migration, starts the existing GitHub/repository checker fixture on another ephemeral loopback port, runs a local Worker using Hyperdrive's local connection, runs `tests/registry.py`, then checks simultaneous authenticated identities, CORS and the shared rate counter. It removes only its own test container and child processes. Evidence is written to `.omo/evidence/workers/`.

For interactive development, copy `.dev.vars.example` to `.dev.vars`, replace the placeholders and set a local database connection without putting it in tracked configuration:

```sh
export CLOUDFLARE_HYPERDRIVE_LOCAL_CONNECTION_STRING_HYPERDRIVE='postgres://USER:PASSWORD@127.0.0.1:5432/ISOLATED_DB'
npm run dev
```

Apply the existing migration to that isolated database first. Keep `.dev.vars`, connection URLs and credentials out of Git.

## Configuration before a future deployment

1. Create a Hyperdrive configuration for the intended PostgreSQL database with **query caching disabled** (`wrangler hyperdrive create <name> --caching-disabled` plus database connection options, or turn caching off in the Hyperdrive dashboard). The registry requires fresh reads immediately after publishes and ownership changes; cached query results can violate that behavior. Replace the all-zero `hyperdrive.id` placeholder in `workers/wrangler.jsonc` with its real ID. Use a PostgreSQL role scoped to the registry database and configure TLS according to the database provider. Apply the existing migration through the normal database administration process. Local connection tests do not exercise Hyperdrive's remote query cache, so verify this setting in the real account before switching traffic.
2. Set `FRONTEND_URL`, `BACKEND_BASE_URL` and the exact comma-separated `CORS_ALLOWED_ORIGINS` for the intended frontend/API URLs. `BACKEND_BASE_URL` determines the GitHub callback URL.
3. Provision `JWT_SECRET`, `GITHUB_CLIENT_ID`, `GITHUB_CLIENT_SECRET` and `ADMIN_GITHUB_ID` using Wrangler secrets. Admin identity is the immutable numeric GitHub ID, not a login. Current sessions require a signed `github_id` claim; legacy sessions without it must sign in again. Configure the GitHub OAuth application's callback as `<BACKEND_BASE_URL>/api/auth/github/callback`.
   Configure a Worker-reachable `REPOSITORY_CHECK_URL` and shared `REPOSITORY_CHECK_TOKEN`; the existing repository-checker service remains necessary for GitHub token identity, source inspection and publishing. A Docker-only hostname is not reachable from a deployed Worker. Keep the service's own GitHub credentials in that service. When artifact storage is used, configure Worker-reachable `STORAGE_BASE_URL`, `STORAGE_PUBLIC_URL` and secret `STORAGE_TOKEN`. With storage unset, the existing Forge behavior leaves artifact metadata empty. Artifact backfill remains an explicit native maintenance operation; Workers do not execute native backend startup/backfill tasks.
4. Keep `nodejs_compat`, the Hyperdrive binding and `RATE_LIMITS` Durable Object binding/migration. The Durable Object migration is required for the existing Forge publish limit to work across instances.
5. Decide the API custom-domain/route mapping and preserve frontend and installer `/api/*` URLs. `workers_dev` is intentionally disabled and no production route is present. Review and explicitly deploy only after these values and the real database connection are verified.

The pinned Wrangler release supports the configured compatibility date. Cloudflare's [node-postgres Hyperdrive guide](https://developers.cloudflare.com/hyperdrive/examples/connect-to-postgres/postgres-drivers-and-libraries/node-postgres/) documents the required compatibility flag and connection pattern. Connections belong to one request and are closed even when connection setup or dispatch fails. Unexpected bridge I/O errors produce HTTP 503; cleanup failures are logged without replacing an already produced response. Logs contain event names and PostgreSQL error codes, not tokens, SQL values or connection strings.

## Limits and verification boundary

Request bodies are limited to 2 MiB, matching the native JSON request limit. The parser rejects duplicate decoded object keys, embedded NUL strings and excessive nesting; JWT expiry/nbf must be safe integers. JSON values use JavaScript numbers; arbitrary integers beyond JavaScript's safe integer range are not lossless. Current registry counts, limits and JWT timestamps fit that range; GitHub identity claims are strings. Do not use this bridge for new arbitrary 64-bit JSON fields without extending its representation. Forge's own generated integer arithmetic uses bigint.

Artifact transport streams up to 256 MiB from the immutable codeload URL selected by Forge. It reads that archive twice: once to compute SHA-256, then again to stream to the storage service's digest URL, avoiding a large isolate memory buffer or temporary filesystem. The storage service verifies the uploaded digest. Network errors fail closed; no archive is registered when upload fails. Its transport is covered by mocked protocol tests; the local registry suite leaves artifact storage unconfigured and does not exercise live GitHub archive downloads or a deployed storage service.

The test suite exercises the local Workers runtime and a real isolated PostgreSQL server. It does not verify a Cloudflare account, a provisioned Hyperdrive connection, DNS, production database connectivity or a live GitHub OAuth exchange. Those require the deployment configuration above. Existing GitHub state/callback policy remains in `auth.fg`; the adapter performs the external HTTP exchange without retries.
