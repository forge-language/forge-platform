# Forge Platform

Forge language website, module registry, package manager and verified installer.

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

Linux x86_64, glibc 2.35+ (Ubuntu 22.04+), with `curl`, `tar`, `sha256sum`, `cc`:

```sh
curl -fsSL https://raw.githubusercontent.com/forge-language/forge-platform/main/scripts/install.sh | bash
source "$HOME/.forge/env"
forge --version
mkdir hello && cd hello
forge init hello-app
forge run
```

Downloads compiler + package manager, checks SHA-256, installs without sudo into
`~/.forge`, and adds a managed PATH block. `--prefix`, `--version`,
`--no-modify-path` and `--uninstall` are supported. Failed checksum validation
preserves the current toolchain. Updates switch a symlink to a fresh version.
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
forge pkg add forge-web 0.1.2
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
flags, tool versions, environment and output contents/permissions. Source or
output changes rebuild; failed builds never publish a key. Native module builds
always run CMake's dependency checks and the final linker, while unchanged CMake
configuration can be reused. Browser builds also validate npm lockfiles and the
installed dependency contents. Unsupported filesystem entries (including
symlinks) and trees beyond the hashing limits build without caching.

## Hosting

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
verified GitHub login; only admins may publish on behalf of another repository
owner. Releases are immutable. Registry manifests reference GitHub HTTPS commits;
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
18 API tests cover validation, ownership, immutable concurrent publishing, exact
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

[Forge compiler](https://github.com/forge-language/forge-preview),
[PostgreSQL](https://github.com/forge-language/forge-postgres),
[Web](https://github.com/forge-language/forge-web),
[Browser](https://github.com/forge-language/forge-browser),
[portfolio migration](https://github.com/Helloworld0822/portfolio-platform/pull/1).
Performance conditions/raw results are in portfolio-platform/docs/forge-performance.md.
