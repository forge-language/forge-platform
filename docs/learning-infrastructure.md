# Learning and editor infrastructure

English is the primary language. The site defaults to English; its Korean language
switch adds translated explanations without changing Forge source or lesson IDs.

- `/learn`: eight sequential lessons, editable programs, real browser execution,
  expected-output checks and exercises. No account or local installation is needed.
- `/play?lesson=int64`: share a specific lesson and download the edited `.fg` file.
- [forge-learning](https://github.com/forge-language/forge-learning): the canonical
  course, verified challenge solutions and a terminal runner for both native and
  JavaScript backends. Its CI compiles and executes all examples and solutions.
- [language-server](https://github.com/forge-language/language-server): native and
  TypeScript releases plus `scripts/doctor.py` for toolchain and stdio checks.
- [vscode-extension](https://github.com/forge-language/vscode-extension): guided
  setup, start/restart and server output commands. Untrusted workspaces retain
  syntax support; server processes require workspace trust.
- Sublime Text uses independent Forge syntax and optional LSP-Forge packages.

## Update the course

Review changes and pass the course CI first, then sync an explicit checkout:

```sh
python3 scripts/sync-learning.py --source ../forge-learning
npm run build --prefix frontend
node frontend/node_modules/@playwright/test/cli.js test --config tests/learning-ui.config.ts
```

The sync command requires committed source bytes and writes provenance containing
an immutable Git revision and SHA-256. It makes no network requests. The website
ships the reviewed snapshot in `frontend/public/learn/`; clean builds do not need
a sibling learning checkout.

Browser tests use the actual WASM compiler. Local frontend development needs the
compiler assets built by `scripts/build-playground.sh` or extracted from a local
site image; the frontend container builds them with pinned Emscripten. The source
stays in the browser. A sandboxed iframe runs programs in a worker with a two-second
execution deadline, output cap and network-disabled production CSP.

Output checks establish the expected observable result of each example. They do
not establish full type safety, ownership safety or correctness for arbitrary
inputs. Browser lessons cover the JavaScript-compatible subset; processes and
native system modules require the local toolchain.

## Validation on 2026-10-08

The eight baseline browser examples passed through the real WASM compiler.
Native and JavaScript course CI executed eight examples and eight reference
solutions in each backend (32 executions). The published site passed course
checks, output mismatch and compiler-error recovery, an execution timeout,
source download, Korean assistance and mobile width checks. Existing site
checks covered registry browsing, recursive functions, UTF-8, exact int64 values,
sandbox network restrictions and documentation routes.

The native and TypeScript server release archives were installed in an isolated
directory, verified with SHA-256 and checked through actual compiler execution
and stdio initialize/shutdown. The VS Code extension passed Linux and Windows
CI, including setup, retries, resource cleanup and the workspace trust gate.

## Dependency execution checks

Current package-manager integration tests require explicit native execution trust
for the exact repository and Git commit recorded in `forge.lock`. The module
smoke test reviews the official pinned test dependencies and grants `forge trust
forge-postgres` and `forge trust forge-web`. Grants are verified against the lock;
npm lifecycle scripts are not enabled by these grants.

Some historical SDK builds lack this command. To check a historical
SDK that reports an unknown command, use `tests/installed-cache.py --allow-legacy-trust` or set
`FORGE_TEST_ALLOW_LEGACY_TRUST=1` for `tests/client-modules.sh`. These explicit
compatibility options accept only the old package manager's unknown-command
response. Other failures still fail the test. Legacy runs do not establish
execution-trust enforcement. Release CI must omit these options when validating
a newly built package manager.
