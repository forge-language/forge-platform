Forge 0.3.0-preview.2 for Linux x86_64 / glibc 2.35+ (Ubuntu 22.04+).

Includes the Forge C backend, experimental JavaScript output, standard runtime,
Forge package manager and browser bundler. OS/protocol/crypto operations use native
bridges. A C compiler is required; module builds also need Git/CMake/pkg-config and
the module's development libraries. Browser builds need Node 22/npm.

Install:

```sh
curl -fsSL https://raw.githubusercontent.com/forge-language/forge-platform/main/scripts/install.sh | bash
source "$HOME/.forge/env"
forge --version
forge init hello-app
forge run
```

The installer checks the attached SHA-256 checksum before extraction and supports
updates and uninstall. Official module metadata is bundled; set FORGE_REGISTRY to
a hosted Forge registry to publish or use additional modules.

Validation: compiler/runtime CTest and bootstrap fixed point, isolated portfolio
API/DB tests, real browser tests, registry API tests, install/update/tamper/uninstall,
Ubuntu 22.04 native module builds and installed browser module execution.

This is a preview, with incomplete type/ownership checks; it does not offer Rust's
memory-safety guarantees. Only the listed Linux platform is distributed.
