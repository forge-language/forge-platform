# Forge

**Safe ownership. Massive concurrency. Native speed.**

Forge is an experimental systems programming language exploring ownership-based
memory safety, lightweight concurrency and native performance. These are design
goals; complete type/ownership checking and Rust-level memory safety are not yet
implemented. Development is an open experiment with human maintainers and AI
coding agents: **AI builds. AI tests. AI reviews. Humans decide.**

Official site: [forge-lang.org](https://forge-lang.org) ·
[Architecture](ARCHITECTURE.md) · [Current specification](LANGUAGE_SPEC.md) ·
[Roadmap](ROADMAP.md) · [RFC process](RFC_PROCESS.md) ·
[First contribution candidates](GOOD_FIRST_ISSUES.md) · [Code of conduct](CODE_OF_CONDUCT.md).

## Repositories

Each component lives in its own repository and directory under the
[forge-language organization](https://github.com/forge-language).

| Repository | Responsibility | Local directory |
| --- | --- | --- |
| [forge](https://github.com/forge-language/forge) | Compiler, bootstrap and compiler fixtures | `forge/` |
| [forge-runtime](https://github.com/forge-language/forge-runtime) | Scheduler, arenas, ownership, events, platform primitives | `forge-runtime/` |
| [forge-stdlib](https://github.com/forge-language/forge-stdlib) | System, string, JSON, networking and other standard modules | `forge-stdlib/` |
| [language-server](https://github.com/forge-language/language-server) | Native and TypeScript language servers | `language-server/` |
| [vscode-extension](https://github.com/forge-language/vscode-extension) | VS Code and Cursor client | `vscode-extension/` |
| [editor-configs](https://github.com/forge-language/editor-configs) | Vim and Neovim plugins | `editor-configs/` |
| [forge-benchmarks](https://github.com/forge-language/forge-benchmarks) | Benchmark harnesses, fixtures and recorded measurements | `forge-benchmarks/` |
| [forge-proofs](https://github.com/forge-language/forge-proofs) | Lean models and proofs | `forge-proofs/` |
| [forge-platform](https://github.com/forge-language/forge-platform) | Website, registry and platform services | `forge-platform/` |
| [forge-web](https://github.com/forge-language/forge-web) | Web framework | `forge-web/` |
| [forge-postgres](https://github.com/forge-language/forge-postgres) | PostgreSQL integration | `forge-postgres/` |
| [forge-browser](https://github.com/forge-language/forge-browser) | Browser integration | `forge-browser/` |

The compiler builds against immutable runtime and stdlib commits listed in
[cmake/Dependencies.cmake](cmake/Dependencies.cmake). The stdlib depends on the
runtime; the runtime has no stdlib dependency. See
[the migration guide](docs/repository-layout.md) for moved paths and local development.

## Build and install

Requirements: CMake 3.16+, Git, a C11 compiler, and network access for the first
configure. Python 3 runs the compiler regressions; Node runs JavaScript backend
regressions. OpenSSL, OpenCL and liburing enable optional standard modules.

```sh
git clone https://github.com/forge-language/forge.git
cd forge
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --build build --target forge-selfhost-verify -j2
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

CMake downloads the pinned library sources into the build directory. To work on
separate local checkouts, pass their paths explicitly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DFORGE_RUNTIME_SOURCE_DIR="$PWD/../forge-runtime" \
  -DFORGE_STDLIB_SOURCE_DIR="$PWD/../forge-stdlib"
```

The build assembles an SDK under `build/` with `bin/`, `include/` and `lib/`.
Use `--forge-root "$PWD/build"` with an explicit build toolchain; installed
compilers locate the SDK relative to their executable.

## Compile and run

```sh
./build/bin/forge examples/hello.fg -o build/hello
./build/hello
./build/bin/forge examples/control_flow.fg --emit-c -o build/control_flow.c
```

The compiler emits temporary C and invokes a C compiler for native binaries.
`--emit-c` keeps C output; `--cc`, `--forge-root`, `--lib-dir` and `-I` select
explicit toolchain and module locations. `--check` validates a source without
producing a binary, and `--symbols-json` emits editor symbol information.
Stage0 checks visible function argument/return types, lexical names and local
initialization before optimization and either backend. Standard-library and
binary-library signatures, complete return-path analysis, ownership aliases and
lifetimes remain outside this check. Initialization analysis is conservative:
both `if` branches must initialize a value; loops and `match` do not establish
initialization afterward. The separate `forge-fg` compiler does not yet share
this stage0 semantic pass.

The language includes functions, pattern matching, constants, pipe expressions,
libraries, processes, coroutines, messages and ownership syntax. Examples under
`examples/` are executable language fixtures, not bundled editor/platform projects.

## Self-hosting

The default build creates `forge-stage1` and `forge-stage2` from
`bootstrap/compiler.fg`. `forge-selfhost-verify` checks that stage2 recompilation
reaches a fixed point. The installed stage2 compiler is named `forge-fg`.
It supports an explicit subset; see
[compiler and self-hosting notes](docs/compiler-and-selfhosting.md).

## External projects

Install the toolchain, then use `find_package(Forge CONFIG REQUIRED)` and
`forge_add_executable` / `forge_add_library` in an external CMake project.

```sh
cmake -S examples/external-project -B build/external \
  -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build/external -j2
./build/external/bin/hello
```

C consumers link `ForgeRuntime::runtime` for runtime APIs and `ForgeStd::stdlib`
for standard-library APIs. The latter exports its runtime dependency.
[External project documentation](docs/examples/external_project.md) has details.

## Editors, proofs and measurements

Build/install the [language server](https://github.com/forge-language/language-server)
against the installed SDK, then install the editor client for your editor.
Compiler builds do not build editor extensions or servers.

Run `lake build` in [forge-proofs](https://github.com/forge-language/forge-proofs).
Benchmarks and historical raw measurements are maintained in
[forge-benchmarks](https://github.com/forge-language/forge-benchmarks), with explicit
SDK and source-checkout inputs.

## Docker

```sh
docker build -t forge-language:local .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" \
  forge-language:local examples/hello.fg
```

The image builder downloads the pinned component repositories and verifies
self-hosting. The runtime image contains the installed SDK and C compiler.

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md). Forge code uses
[Apache License 2.0](LICENSE); bundled third-party code retains its own notices.
