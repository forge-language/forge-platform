# Public SDK download recovery — 2026-10-05

This continues the public installation follow-up from
[the initial deployment](deployment-2026-10-05.md).

## Observed behavior

The 0.3.0-preview.3 SDK is 13,340,047 bytes. A local 1 MiB range returned
HTTP 206 in 0.017662 seconds (59,369,040 bytes/second). The same range through
the public Cloudflare LAX route reached 999,334 bytes before its 25-second
probe deadline (39,973 bytes/second). Separate 256 KiB HTTP/1.1 and HTTP/2
probes completed in 5.55–5.57 seconds, both around 47 KB/second. Public API
health remained HTTP 200. These are measurements from this server and route,
not a guarantee of other clients' bandwidth.

At this rate, a complete archive can exceed the previous installer's single
300-second download deadline. The archive itself and its checksum were kept.

## Changes

- Archive downloads have three attempts, each bounded to 300 seconds, and
  resume from the partial file using HTTP Range. Transient connection/HTTP
  errors retry; a permanent 404 fails immediately. A server that rejects
  resume restarts the archive within the same attempt budget.
- The checksum is fetched and validated first, with bounded transient retries.
  The full archive SHA-256 must match before extraction and toolchain switching.
  Failed downloads and changed/corrupt resumed content preserve the active
  toolchain; temporary downloads are removed.
- The site entrypoint now rewrites only the installer's default-origin
  assignment. It previously also overwrote the trailing-slash normalization,
  producing doubled slashes for an origin ending in `/`.
- Public installation checks allow 1050 seconds for installation while local
  checks retain 180 seconds. The end-to-end check downloads the installer with
  the documented curl client. Python urllib's public fetch returned HTTP 403
  on this route while curl and browser requests succeeded.

Retrying improves completion on interrupted or slow links; it does not increase
the measured Cloudflare transfer bandwidth. Resume data is retained within one
installer invocation, not across separate invocations.

## Deployment and validation

Only the dedicated Forge site service was recreated. The running prior site
image was preserved as `forge-platform-site:rollback-download-20261005`.
The API and PostgreSQL containers were retained. Nginx configuration and the
deployed `/install.sh` origin/normalization/resume options were checked.

Completed checks:

- Installer regression suite: 11 tests passed. The new transfer test exercises
  dropped connections, unsupported ranges, checksum/SDK HTTP 503 recovery,
  permanent 404, exhausted retries and altered resumed content. Another test
  checks repeated site-origin rewriting and installation with a trailing slash.
- Isolated registry API: 20 tests passed, including the DB-offline stateless checks.
- Local website/browser: 6 tests passed in 7.5 seconds.
- Public HTTPS website/browser: 6 tests passed in 34.9 seconds.
- Locally installed SDK: native/browser cache invalidation, official Git pins,
  BigInt and UTF-8 checks passed with the updated end-to-end harness.
- Shell/Python syntax and Git whitespace checks passed.

- Public-origin end-to-end verification passed: `/install.sh` and the complete
  SDK were downloaded from `https://forge-lang.org`; checksum validation,
  installation, native compilation/execution and cache invalidation, official
  browser/web Git pins, JavaScript cache repair, BigInt/UTF-8 execution and
  uninstall all completed successfully. Isolated profile files stayed untouched.

Reproduction:

```sh
python3 tests/installer.py
sh scripts/test.sh
python3 tests/installed-cache.py --local-release
python3 tests/installed-cache.py --origin https://forge-lang.org
FORGE_SITE_ORIGIN=https://forge-lang.org node frontend/node_modules/@playwright/test/cli.js test --config tests/playwright.config.ts
```

The preserved SDK SHA-256 is
`75135ca08191047533ac0d5d8ed7b64bda87de29fc4c37cb8cd7264ceb0d101d`.
The deployed site image is
`sha256:46eaf178e3b562996a020cf89123bd4e7a6c740799c61a7582dcbd20659a0b11`.
The final publicly served installer matches the reviewed source with the
configured default origin and has SHA-256
`3765a1479e8d3db320e40e00c6341ea3b3266c646553549934d0b2ca6c9e089f`.

GitHub OAuth/Discord configuration remains as recorded in the initial deployment.
