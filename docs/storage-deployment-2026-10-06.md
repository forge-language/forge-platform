# Redis, file distribution and website deployment — 2026-10-06

Forge Platform now caches public registry GETs in its own private Redis, distributes package source archives and SDKs through Forge Storage, and serves the English-default website with Korean selection.

## Services and source

- Site: https://forge-lang.org
- Object service: https://storage.forge-lang.org/health
- Library and service repository: https://github.com/forge-language/forge-storage
- Container: `ghcr.io/forge-language/forge-storage:latest` (GitHub Actions builds/tests/publishes on main; authenticated pull is required by the current package visibility). The public repository supports anonymous local image builds.
- Local compiler container: `forge-lang:latest`; build instructions in the compiler repository's `docs/docker.md` and `Containerfile`. The existing Dockerfile was retained.

The storage service uses Forge for routing, writer authorization, key validation and library orchestration. C bridges supply HTTP, disk streaming, hashing and bounded file transfers. Content-addressed objects are immutable, public to read, and require a server writer token for uploads. Uploads verify SHA-256 before atomic publication. Downloads support HEAD, ETag and single byte ranges. This is an S3-like distribution system, not the AWS S3 API; storage is a persistent local volume behind Cloudflare Tunnel.

The API creates package archives from the exact GitHub commit after checking publisher/owner permissions. Caller-provided artifact URLs cannot choose the stored source or download origin. Existing release versions, commits and publication dates were preserved during the metadata backfill. CLI installs verify archive checksum and size, reject unsafe archive entries and use a staging directory. Legacy manifests still use Git; archive failures do not silently switch transports.

SDK preview.6 bundles the new package manager and an artifact-aware official catalog, including Forge Storage 0.1.1. The library snapshots expected hashes before hashing downloaded files and returns stable Forge digest strings; a regression verifies that incorrect bytes cannot pass through an aliased native buffer. Archive extraction scopes a UTF-8 locale to its thread so Korean PAX filenames work without changing other threads. The release-only libarchive build omits unused XML/ICU dependencies and reduces the SDK from approximately 28.7 MB to 14.8 MB. Its source version and checksum are pinned. `scripts/publish-artifacts.py` uploads SDK/checksum files and atomically writes Nginx redirects; `nginx -t` and reload activate the map. Older SDK objects remain available.

## Deploy and maintain

Credentials remain in host `.env` files with mode 0600. Do not commit or copy the storage writer token into frontend configuration. The storage root is a dedicated Compose volume; PostgreSQL's existing volume was preserved. Back up both volumes separately.

```sh
cd /home/helloworld0822/coding/forge-storage
docker build -f Containerfile -t forge-storage:local .
docker compose up -d --wait
cd /home/helloworld0822/coding/forge-platform
docker compose -f compose.yml -f deployment/storage-network.yml up -d --no-build --wait --wait-timeout 360
```

The production override attaches only the Forge API to the dedicated storage network. The ordinary Compose file remains suitable for isolated registry tests with storage disabled. `STORAGE_BACKFILL_ON_START=1` explicitly opts in to one-time metadata backfill; production has been returned to `0` after successful migration. An enabled backfill rejects startup on failure and is safe to repeat.

Cloudflare Tunnel routes `storage.forge-lang.org` to loopback port 18104. The storage proxy enforces read/write and connection limits. Core application ports, Redis and PostgreSQL are not published publicly. The activity timer polls repositories every 60 seconds with conditional GitHub requests; active pages poll snapshots every 30 seconds. Failed repository polls retain prior evidence and expose errors/freshness instead of invented data.

## Security and cache behavior

- General site limit: 60 requests/second per client, burst 120; connection limit 40.
- Login/callback limit: 5/minute, burst 5. Package POST limit: 20/minute, burst 5. The Forge publisher also enforces 30 publications/10 minutes per authenticated user.
- Storage limits: 60 reads/minute, burst 30; 20 PUTs/minute, burst 5; 20 concurrent connections; object limit 256 MiB (public proxy/provider limits may be lower).
- Client IP headers are trusted only from the tunnel's Docker host gateway. Other forwarded headers cannot bypass limits.
- CSP, nosniff, referrer policy, permissions policy and HSTS apply to cached/static resources too. The execution iframe keeps a separate opaque, network-disabled sandbox policy.
- Dotfile paths are denied; request/header/body timeouts and body limits are configured. Existing JWT expiry, owner checks, parameterized SQL and OAuth state checks remain in place.
- Redis stores only successful public JSON responses, maximum 256 KiB, for 30 seconds. A UUID namespace rotates after successful publish, so late query completion cannot refill the current namespace with old data. Redis failure falls back to PostgreSQL; a failed invalidation can leave prior data until its bounded TTL expires. Redis is memory-limited to 64 MiB and has no public port or persistence.

## Website

The homepage defaults to English and saves a user's Korean selection locally. `/docs/syntax` covers print/println, variables, conditions, for/while loops, functions and modules, with compiled examples. `/view?file=/project/LANGUAGE_SPEC.md` renders bounded Markdown/JSON with escaped text and constrained links. Benchmarks and Agents show real repository evidence and PR state with freshness, rather than agent participation inferred from authors. The registry search has a publishing button on its right; package details provide verified source-archive links and hashes.

Page components and the Playground are loaded on demand. Gzip and explicit cache policies reduce transfer costs. See the measured [site report](site-performance-2026-10-06.md) and [Redis report](redis-cache-performance-2026-10-06.md); the small local benchmark is not a production throughput guarantee.

## Validation

- Isolated registry API: 20 tests; Redis behavior: 4; site browser: 8; security: 8 — passed in the required `scripts/test.sh` run.
- Package archive transport: 11 tests, including unsafe archives, checksum/size mismatch, lock preservation, cache reuse and legacy Git compatibility — passed.
- Real backend-to-storage integration: publication, metadata backfill, idempotency, checksum/size, fixed source selection and immutable release conflict — passed.
- Object service: binary upload, authorization, mismatch rejection, HEAD/Range/ETag, concurrent duplicate uploads, size/chunked transfer bounds and restart persistence — passed. GitHub Actions service tests and GHCR publishing passed.
- SDK installer and installed native/browser behavior were checked locally. Public site browser cases passed; one navigation hit a transient Chromium network change and passed unchanged on retry. Public read-only security checks passed. A header-only oversized-body probe waited at Cloudflare, while the same limit passed against isolated Nginx.
- Public console checks found no application runtime errors; Cloudflare's injected analytics beacon is blocked by the existing CSP.

GitHub OAuth client credentials remain unset. One-time GitHub-token login supports publishing without an OAuth application; the identity token is verified and discarded, and the browser retains only a one-hour registry session. Official package ownership uses the immutable numeric administrator ID.

Rollback images are `forge-platform-backend:rollback-storage-20261006` and `forge-platform-site:rollback-storage-20261006`; private configuration/database backups are in `.deployment/20261006-storage/`. The backend's prior image metadata was unavailable, so its running filesystem was exported/imported without copying container environment secrets; Compose restores runtime configuration for rollback.

## Resumed verification — 2026-10-09

The current preview.6 SDK, public installer, native storage library, live refresh and public learning UI passed renewed checks. See [the verification record](resume-validation-2026-10-09.md). The newer `forge-public-refresh.timer` replaces the original activity timer.
