# Forge Platform

Forge language website, module registry, package manager and verified installer.

Official site: [forge-lang.org](https://forge-lang.org).
**Safe ownership. Massive concurrency. Native speed.** are experimental design
goals, not current safety guarantees. The site provides Learn / Try / Watch /
Contribute paths, a real compiler Playground, reproducible benchmark evidence and
public GitHub development snapshots. AI builds/tests/reviews; humans decide.

Production domain, Cloudflare Tunnel, updates and rollback:
[deployment guide](deployment/README.md).

- `frontend/`: React 19, TypeScript, Tailwind 4 and Vite.
- `backend/src/`: Forge HTTP routes, package validation, ownership, OAuth and SQL.
- `cli/src/`: Forge dependency resolution, Git pinning and native/browser build policy.
- `cli/native/`: generic atomic filesystem writes and bounded argv subprocesses.
- `vendor/forge-{postgres,web}`: independent Git submodules, native protocol bridges.
- `vendor/toolchain/`: pinned Forge compiler/runtime sources, including JavaScript output.

The compiler and runtime are still experimental. Native OS/protocol/crypto bridges
are allowed; this does not claim Rust ownership/type safety or a fully self-hosted
compiler implementation.

## Install

Linux x86_64, glibc 2.35+ (Ubuntu 22.04+), with `curl`, `tar` and `sha256sum`. Installation does not require GCC or another C compiler. A C compiler is needed later to build Forge programs; native modules also need their documented development tools and libraries:

```sh
curl -fsSL https://forge-lang.org/install.sh | bash
source "$HOME/.forge/env"
forge --version
forge init hello-app
cd hello-app
forge run
```

`forge init hello-app` creates a new `hello-app/` directory. Enter it with `cd hello-app` before running project commands. `forge init` uses `forge-app/`; `forge init .` initializes the current directory. Existing named directories are never overwritten.

Downloads compiler + package manager, checks SHA-256, installs without sudo into
`~/.forge`, and adds a managed PATH block. `--prefix`, `--version`,
`--no-modify-path` and `--uninstall` are supported. Failed checksum validation
preserves the current toolchain. Updates switch a symlink to a fresh version.
Archive downloads allow three attempts of up to five minutes each. Interrupted
transfers resume from the received bytes when the release host supports ranges;
hosts without range support restart the download. Permanent HTTP errors stop
immediately, and exhausted retries leave the current toolchain in place.
Checksums are downloaded through HTTPS from the release host; they are integrity
checks, not independent signed attestations.

```sh
forge update
forge uninstall
```

GitHub installation defaults to a bundled official catalog. This lets developers
install the official modules before a public registry domain is configured:

```sh
forge pkg search postgres
forge pkg add forge-postgres 0.1.1
forge pkg add forge-web 0.1.4
# Review the pinned dependency sources before allowing native builds.
forge trust forge-postgres
forge trust forge-web
forge build
forge pkg list
```

Module builds require Git/CMake/pkg-config and native development libraries:
PostgreSQL: `libpq-dev`; web: `libmicrohttpd-dev libjson-c-dev
libcurl4-openssl-dev libssl-dev`. Exact x.y.z versions only; Git commits are fixed
in `forge.lock`. Cycles/conflicts and graphs beyond 64 modules / 32 levels fail.
Native module CMake files execute locally as part of the build.

Browser output needs Node 22 and npm:

```sh
forge pkg add forge-browser 0.1.3
forge build --emit-js
# Load build/app.js from an HTML page containing <div id="app"></div>.
```

Use the hosted registry instead of the bundled official catalog:

```sh
export FORGE_REGISTRY=http://localhost:18101
forge pkg search web
# Login on the website, display the CLI token, then set FORGE_TOKEN locally.
forge pkg publish module.json
```

## Incremental builds

Unchanged native applications without external native modules reuse compiler/link
outputs. The key checks project/import contents, manifests, the compiler/runtime,
flags, resolved compiler/tool executable contents, tool versions, environment
and output contents/permissions. Source or
output changes rebuild; failed builds never publish a key. Native module builds
always run CMake's dependency checks and the final linker, while unchanged CMake
configuration can be reused. Browser builds also validate npm lockfiles and the
installed dependency contents. Unsupported filesystem entries (including
symlinks) and trees beyond the hashing limits build without caching.

## Hosting

The active public origin is https://forge-lang.org. Public registry configuration:
`FORGE_REGISTRY=https://forge-lang.org`. GitHub-token login supports publishing
without an OAuth app; OAuth remains optional.

```sh
GIT_MASTER=1 git clone --recurse-submodules https://github.com/forge-language/forge-platform.git
cd forge-platform
npm ci --prefix frontend
# Generate a NEW .env for this stack; never copy production secrets.
cp .env.example .env
# Replace JWT_SECRET and POSTGRES_PASSWORD with random hexadecimal secrets.
docker compose up -d --build
python3 tests/registry.py --seed-site
```

Default binding: `127.0.0.1:18101`. PostgreSQL has no host port. New registry data
uses its own named volume. Set FRONTEND_URL/BACKEND_BASE_URL and the site download
origin before connecting a public HTTPS reverse proxy. Existing applications,
production databases, DNS and TLS configurations are not modified by this stack.

GitHub OAuth requires a separate OAuth application's ID/secret and callback
`<BACKEND_BASE_URL>/api/auth/github/callback`. Public registry reads and downloads
work without OAuth. Authenticated publishing binds package ownership to the
verified immutable GitHub account ID. The configured numeric administrator
may register official forge-language organization repositories. Releases are immutable. Registry manifests reference GitHub HTTPS commits;
the client verifies checkout SHA and module presence when installing.

## Validation

```sh
sh scripts/test.sh
python3 tests/installer.py
# Release build (Ubuntu 22.04, glibc rather than musl):
docker build -f Release.Containerfile -t forge-platform-release:local .
docker run --rm -v "$PWD:/src" -e BUILD_DIR=/src/build/glibc forge-platform-release:local sh -c 'sh scripts/build.sh && sh scripts/release.sh'
python3 tests/installed-cache.py --local-release
```

Tests use disposable PostgreSQL, an isolated Compose project and port 18103.
API tests cover validation, ownership, immutable concurrent publishing, exact
versions, numeric sorting, errors and parallel reads. Browser tests cover real
registry navigation/search/details and responsive anonymous publishing. Installer
tests cover repeated installation, compiling/running Forge, package pinning,
checksum corruption, protected directories and uninstall. `tests/client-modules.sh`
builds installed PostgreSQL/web modules on Ubuntu 22.04; `tests/client-browser.sh`
builds an installed browser module using the distributed manager and bundler.
`tests/build-cache.py` checks stage invalidation with controlled tools;
`tests/installed-cache.py` installs the real release and checks native/browser cache
hits, input/output invalidation, BigInt, UTF-8 and conservative symlink fallback.

## Related projects

[Forge compiler](https://github.com/forge-language/forge),
[PostgreSQL](https://github.com/forge-language/forge-postgres),
[Web](https://github.com/forge-language/forge-web),
[Browser](https://github.com/forge-language/forge-browser),
[portfolio migration](https://github.com/Helloworld0822/portfolio-platform/pull/1).
Performance conditions/raw results are in portfolio-platform/docs/forge-performance.md.

## Security and resource bounds

The HTTP bridge rejects decoded NUL characters, duplicate JSON keys (including
escaped equivalents), out-of-range int64 values and non-finite numbers. JWT
verification authenticates the bounded signed bytes before parsing JSON, uses
constant-time HS256 verification and enforces integer `exp`/optional `nbf`.

`X-Real-IP` is ignored unless the socket peer matches `FORGE_TRUSTED_PROXIES`.
Compose trusts only the site container's fixed IP on a dedicated network, using
`FORGE_PROXY_SUBNET=172.28.241.0/29` and `FORGE_PROXY_IP=172.28.241.3` defaults.
Override both together for network conflicts. Direct clients cannot select
their identity through this header. OAuth return paths are validated before
signing and capped at 256 characters.

Stateless health, preflight and authentication routes do not acquire PostgreSQL
leases. Registry SQL uses the bounded per-connection prepared-plan cache; release
ownership, transaction locks and immutable versions are still checked against
database state. Bounded public registry GET responses use a private Redis cache
with a 30-second TTL and a namespace rotation after committed publication;
authentication and errors are never cached. Redis failure falls back to PostgreSQL.

The installer checks archive names and types before extraction and accepts only
regular files/directories, without setuid/sticky permissions, within 1 GiB and
100,000 entries. Unsafe or invalid archives preserve the active toolchain. A
checksum fetched from the same release origin detects transfer corruption; it
is not an independent publisher signature. Released binaries must be rebuilt
to include native bridge fixes; editing the installer cannot patch an existing
SDK by itself.

## GitHub repository registration and security

Open `/publish` and verify a GitHub personal access token once. The token is sent
only for server-side GitHub identity verification, is not logged or stored, and is
replaced in the browser with a one-hour Forge session. OAuth login remains optional.
Personal repositories must belong to the same immutable GitHub account ID.
The configured numeric `ADMIN_GITHUB_ID` may register official forge-language
organization repositories. Other organizations are not implicitly trusted.

Enter a canonical public repository URL, ref and manifest path (`module.json`,
with `forge.json` as the default fallback). README content is verified at the same commit. The service
resolves the ref to an immutable commit, verifies Git blob hashes, reads the
manifest and module from that commit, and checks bounded source files without
executing repository code. High-risk patterns, symlinks, submodules, unavailable
source or incomplete checks cannot silently register a release. Native/JavaScript
bridges and build scripts require source-review acknowledgement. The resulting
source SHA and inspection report are stored with the immutable version.
Static inspection is not a malware-free guarantee.

`POST /api/packages/inspect` previews a checked repository;
`POST /api/packages/register` repeats verification at the confirmed SHA and
registers the source manifest. The existing manifest publication endpoint also
verifies repository source and rejects mismatched client metadata. Ownership and
administrator privileges use GitHub numeric IDs, never reused usernames. Legacy
package ownership with no numeric ID is locked to an explicit administrator
migration; existing release contents remain immutable.

See [package execution trust](docs/package-security.md) and
[live data cadence and deployment](deployment/LIVE_REFRESH.md).
