# forge-lang.org deployment — 2026-10-05

## Result

Forge Platform is deployed at https://forge-lang.org through its own Cloudflare Tunnel. HTTPS requests reach the loopback-bound site and the private Forge registry backend. www redirects to the canonical domain; play redirects to /play; packages redirects to /packages and also serves registry API reads.

The old compiler performance work was preserved. A new official-site foundation follows the supplied strategy with real code execution, public evidence, human governance and accurate preview limitations.

## Application and assets

- Site: React / TypeScript / Tailwind, graphite/orange/blue SVG mark and bundled Noto Sans KR fonts.
- Playground: real WASM compiler, disposable compile worker and opaque-origin execution sandbox; 64 KiB input, 64 MiB compiler memory, 2-second execution and bounded output.
- Activity: unauthenticated GitHub snapshot, atomically updated every 15 minutes.
- Source documents: architecture, current specification, roadmap, conduct, RFC process and 12 first-contribution drafts.
- Evidence: checked-in content snapshots with dated reports and raw measurements.
- SDK: 0.3.0-preview.3, rebuilt from current platform/native module sources on Ubuntu 22.04; SHA-256 archive/checksum served from the releases mount. Previous archives preserved.

## Verification

- Isolated registry API: 20 tests passed, including authorization, immutable publishing, validation and database-offline stateless paths.
- Browser: 6 tests passed locally; public HTTPS suite initially passed all 6. After adding fonts, 5 passed and one hit the 30-second page-load deadline; the affected test passed on public HTTPS with an appropriate network deadline. Playground readiness also now handles iframe load independently of the ready message.
- Installer: 9 tests passed against the locally served new release.
- Installed native/browser cache, official Git pins, BigInt and UTF-8 checks passed.
- Existing compiler: 20 stage2 regression tests passed; bootstrap fixed-point verification passed.
- DNS/TLS, HTTP→HTTPS and alias redirects, /api/health, real package details, installer origin/version, WASM MIME and Nginx configuration verified.
- Public Korean font loading and mobile rendering checked visually.
- Final Playground startup shows loading state and uses a visually hidden, renderable sandbox frame. Both public HTTPS Playground tests passed after this update (31.7 seconds).
- Native web bridge CTest checks passed on the rebuilt Ubuntu release.
- Git whitespace checks and Python syntax checks passed.

Large public SDK downloads from this server were slow through Cloudflare's LAX route. The initial public installer suite exceeded its subprocess download deadline; a parallel range probe also timed out. These initial runs are not reported as passing end-to-end public installation. Local release integrity/install/build checks passed, and the public release/checksum endpoints returned correct HTTP metadata. The subsequent [SDK download recovery](sdk-download-recovery-2026-10-05.md) adds bounded resume/retry behavior and records a successful full public-origin installation and installed-module verification. Transfer bandwidth remains slow on the measured route.

## Persistence and rollback

cloudflared-forge.service and forge-activity.timer are enabled. User lingering is active. The PostgreSQL registry volume was kept. A private .env backup and pg_dump are in ignored .deployment/20261005/.

The prior running API image is tagged rollback-20261005. The prior site image had already lost its Docker image metadata and could not be tagged from the running-container image ID; an existing tested previous-site image was preserved as the site rollback reference instead. This is not claimed to be a bit-for-bit snapshot of the previously running site.

## Remaining setup

GitHub OAuth credentials are empty, so authenticated login/publishing is unavailable. Anonymous package reads and website use work. Discord is not created and no invitation is fabricated. First issues are drafts, not posted issues. Complete language safety/concurrency goals and live agent telemetry remain roadmap milestones.

No GitHub commit/PR/release publication, merge or community announcement was performed by this deployment.
