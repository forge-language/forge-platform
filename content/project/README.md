# Forge

**Safe ownership. Massive concurrency. Native speed.**

Forge is an experimental systems programming language exploring ownership-based
memory safety, lightweight concurrency and native performance. These are design
goals; complete type/ownership checking and Rust-level memory safety are not yet
implemented. Development is an open experiment with human maintainers and coding
agents.

Official site: [forge-lang.org](https://forge-lang.org) ·
[Architecture](ARCHITECTURE.md) · [Current specification](LANGUAGE_SPEC.md) ·
[Roadmap](ROADMAP.md) · [RFC process](RFC_PROCESS.md) ·
[First contribution candidates](GOOD_FIRST_ISSUES.md) · [Code of conduct](CODE_OF_CONDUCT.md).

> Development is under the [forge-language](https://github.com/forge-language) organization.
> The active compiler preview is [forge-preview](https://github.com/forge-language/forge);
> editor projects live in [language-server](https://github.com/forge-language/language-server)
> and [vscode-extension](https://github.com/forge-language/vscode-extension).

A **Hybrid Lightweight Process + Coroutine** language — an AOT-compiled language that combines Elixir/Erlang-style lightweight processes with coroutines.

Forge source (`.fg`) is compiled to native binaries through a C backend. The driver writes a temporary C file, invokes the selected C compiler, and removes intermediates by default. Use `--emit-c` for persistent C output or `--keep-temp` to retain intermediates.

## Features

- **Direct native compilation** — `forge app.fg` produces the native executable `app` in the current directory; `-o` selects another output
- **Light Process** — unit for state ownership, isolation, and fault recovery (`process`)
- **Coroutine** — lightweight execution flows inside a process (`coroutine`, `spawn`, `yield`)
- **AOT compilation** — `.fg` → native binary (C emitted only with `--emit-c`)
- **Functions** — top-level `fn` with recursion, forward declarations, and return types
- **Native programs** — `native main` for plain C entry points (bootstrap compiler)
- **Optimizer** — constant folding and algebraic simplification
- **Standard modules** — I/O, strings, math, files, TCP/UDP, HTTP, JSON
- **User libraries** — build static libraries with `library` / `export` / `import`
- **M:N scheduler** — multi-threaded worker pool with work-stealing queues (`fr_scheduler_create(0)` = auto CPU count)
- **Event loop** — epoll (Linux), kqueue (macOS), or select (Windows); `await fd` in coroutines yields until readable
- **Pipe operator** — `value |> fn()` passes value as the first argument (Elixir-style)
- **Pattern matching** — `match expr { pat => stmt, _ => default }` on integers
- **Comptime constants** — `const NAME = expr` folded at compile time
- **Arena allocator** — bump allocation for HTTP request bodies (per-request reset)
- **Cooperative scheduling** — a budget of 2000 coroutine resumptions; long-running coroutine bodies must yield explicitly
- **Ownership** — `own let` for heap strings, `move(x)` and `send proc, tag, move(msg)` for move semantics
- **Supervisor declarations** — restart-policy registration; automatic fault recovery is not implemented

## Requirements

- GCC or Clang (C11)
- CMake 3.16+
- **Linux** — epoll event loop (recommended for production I/O)
- **macOS** — kqueue event loop
- **Windows** — MSVC or MinGW; select-based event loop; Winsock networking

Supported platforms: Linux, macOS, Windows 10+.

- **Self-hosting bootstrap** — `bootstrap/compiler.fg` compiles Forge subset to C; stage2 recompiles itself

## Self-hosting bootstrap

```bash
cmake --build build --target forge-selfhost forge-selfhost-test forge-selfhost-verify
```

Pipeline:

| Stage | Binary | Compiles |
|-------|--------|----------|
| 0 | `build/bin/forge` (C) | `bootstrap/compiler.fg` → `forge-stage1` |
| 1 | `build/bin/forge-stage1` | `compiler.fg` → `forge-stage2.c` |
| 2 | `build/bin/forge-stage2` | self + `examples/match.fg` |

`forge-selfhost` is built by default. `forge-selfhost-verify` builds stage3 from stage2 output and checks that recompiling `compiler.fg` yields identical C (fixed point).

Stage2 also drives native compilation without a shell:

```bash
./build/bin/forge-stage2 examples/control_flow.fg --forge-root "$PWD"
./control_flow
./build/bin/forge-stage2 bootstrap/compiler.fg --emit-c -o /tmp/stage3.c
```

The FG compiler supports an explicit subset: integer/string/void functions, initialized typed bindings, integer matching, loops, and the `strings`, `fs`, `os`, and `io` builtins. It accepts `native main` and a sequential `process main`. It rejects unsupported modules, coroutines, floats, booleans, arrays, and malformed delimiters. It does not yet replace the C compiler's full module, type, or ownership handling. For string variables and custom functions returning strings, use `print_str(value); println();` in this subset.

Verify stage2 compiles itself:

```bash
./build/bin/forge-stage2 bootstrap/compiler.fg -o /tmp/stage3.c
```

## Build

```bash
git clone https://github.com/forge-language/forge.git
cd forge-preview

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Install both compilers and their headers/static libraries:

```bash
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
forge examples/hello.fg
./hello
forge-fg examples/control_flow.fg
```

Installed compilers locate `include/` and `lib/` from their executable path. Native compilation requires a C compiler. `--forge-root`, `--lib-dir`, and `--cc` select explicit locations; `FORGE_ROOT` and `CC` supply defaults. C emission remains available with `--emit-c` or a `.c` output filename.

### Docker

```bash
docker build -t forge-language:local .
docker run --rm forge-language:local --version
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" forge-language:local examples/hello.fg
docker run --rm -v "$PWD:/work" --entrypoint /work/hello forge-language:local
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" --entrypoint forge-fg forge-language:local examples/control_flow.fg
```

The image includes the native compiler, FG compiler, C toolchain, headers and libraries. Its build verifies self-hosting to a fixed point. OpenCL is disabled in this portable image.

### Windows (MSYS2 / MinGW)

```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```bash
./build/bin/hello
./build/bin/coroutines
./build/bin/stdlib_demo
./build/bin/use_library
./build/bin/use_module
./build/bin/http_server   # single-request demo
./build/bin/web_server    # curl http://127.0.0.1:8080
./build/bin/ownership     # own let + move demo
./build/bin/event_echo    # await + epoll TCP echo (port 19090)
./build/bin/pipe          # pipe operator demo
./build/bin/match         # pattern matching + const demo
```

Step-by-step tutorials for every example live in [docs/examples/](docs/examples/README.md).

### Simple Web Servers

**Forge** (`examples/web_server.fg` → `web_server` binary):

```bash
./build/bin/web_server
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/health
```

**Python** (for comparison, see `benchmark/python/server.py`):

```bash
python3 benchmark/python/server.py   # port 19081
curl http://127.0.0.1:19081/
```

**Phoenix** (Elixir, see `benchmark/phoenix/run_server.sh`):

```bash
./benchmark/phoenix/run_server.sh   # port 19082 (requires Erlang + Elixir)
curl http://127.0.0.1:19082/
```

**Rust Axum** (see `benchmark/axum/run_server.sh`):

```bash
./benchmark/axum/run_server.sh   # port 19083 (requires Rust/cargo)
curl http://127.0.0.1:19083/
```

## HTTP Benchmark (Forge vs Python vs Phoenix vs Rust Axum)

Both servers return `Hello, World` (13 bytes) with `Connection: close`.
Benchmark uses **1,000,000 requests** at **1,000 concurrent** connections on Linux.

| Implementation | Port | Requests/sec | Notes |
|----------------|------|--------------|-------|
| **Forge** (AOT native, epoll + REUSEPORT + connection pool) | 19080 | **~12,600** | `benchmark/forge/bench_server.fg` |
| **Rust** (Axum, release) | 19083 | **~8,000** | `benchmark/axum/bench_server` |
| **Python 3** (stdlib socket) | 19081 | **~7,700** | `benchmark/python/server.py` |
| **Phoenix** (Bandit, minimal router) | 19082 | **~7,500** | `benchmark/phoenix/bench_server` |

Measured on: Linux 7.1.2-arch3-1 x86_64 (2026-07-20). Results vary ±5% run-to-run.

Forge exceeds raw Python socket performance in this micro-benchmark.
The benchmark uses prebuilt responses, per-core epoll workers with `SO_REUSEPORT`, and a fast accept→respond→close path (no per-client epoll registration).

### Why Forge was slower (and what we fixed)

The original Forge benchmark path paid avoidable per-request cost:

| Bottleneck | Impact |
|------------|--------|
| `malloc`/`free` on every request to buffer HTTP headers | Heap churn |
| Two `send()` syscalls (header + body separately) | Extra syscall |
| `recv` loop until `\r\n\r\n` instead of one read | Extra syscalls |
| Per-request `snprintf` + `strlen` to build the response | CPU overhead |
| Path parsing + `str_eq` in the Forge handler loop | Unnecessary work vs Python's fixed 200 |
| `setsockopt(TCP_NODELAY)` on every accepted socket | Extra syscall per connection |

**Fixes applied:**

- Stack-buffered header read (no per-request `malloc`) for the general `http_accept` path
- Single-buffer `http_respond` (one `send` when response fits in 576 bytes)
- `http_prepare` / `http_serve_forever` — single-threaded epoll accept loop
- `http_serve_mt(server, n)` — `2×CPU` REUSEPORT accept workers (`n=0`) + `8×` sharded connection handler threads
- Linux fast path: `accept4(SOCK_NONBLOCK)` then immediate respond/close (no client epoll)
- Worker threads pinned to CPUs; listen backlog 4096 and 64 KiB socket buffers

### Run the benchmark yourself

```bash
cmake --build build --target bench_server
./benchmark/run_benchmark.sh
```

Ensure ports **19080**–**19083** are free before running (`fuser -k 19080/tcp 19081/tcp 19082/tcp 19083/tcp` if a prior run left servers behind).

Results are written to `benchmark/results.txt` (gitignored).

## Current limitations

Forge is an experimental language. `--check` parses, loads modules, and runs the optimizer; it does not perform complete type or ownership checking. Ownership diagnostics currently run during code generation. Scheduler synchronization, coroutine control flow, and fault recovery need further work before production use.

The Lean proofs cover an abstract expression model, not the entire C compiler or runtime. In particular, the model uses unbounded integers and does not cover C signed overflow, floating-point behavior, or effects.

The HTTP results describe a cached-response C runtime path with minimal request handling. They do not establish that Forge is generally faster than other languages or full HTTP frameworks.

A source-based review, verified fixes, and reproducible regression commands are recorded in [the September 2026 review](docs/language-review-2026-09-30.md).

The native CLI, installation, FG subset and Docker workflow are documented in [the compiler guide](docs/compiler-and-selfhosting.md). New measurements and the controlled portfolio/Rust comparison are in [the October continuation report](docs/continuation-2026-10-03.md).

## Runtime: M:N Scheduler + Event Loop

Forge processes compile to a **multi-threaded M:N scheduler**:

```
CPU workers (pthread)
    └── work-stealing run queues
            └── light processes
                    └── coroutines (cooperative, switch-case codegen)
```

- `fr_scheduler_create(0)` — `0` means auto-detect CPU count (used by generated `main`)
- Process mailboxes are mutex-protected; `recv()` blocks on a condition variable
- The scheduler embeds an **epoll** event loop; `await` in coroutines registers the fd and yields

```forge
import tcp;

process echo {
    coroutine once(port: int) {
        let sock: int = tcp_listen(port);
        await sock;                        // yield until connection is ready
        let client: int = tcp_accept(sock);
        await client;
        let data: string = tcp_recv(client);
        tcp_send(client, data);
        tcp_close(client);
    }
    spawn once(19090);
}
```

See `examples/event_echo.fg`.

## Ownership (move semantics)

Process-local data is freely usable. Heap strings can be marked **owned** and **moved** across `send`:

```forge
process main {
    coroutine demo() {
        own let msg: string = "owned by coroutine";
        yield;
        println(msg);
    }
    spawn demo();
}
```

- `own let x: string = ...` — owned heap string (process scope)
- `move(x)` — transfer ownership (`fr_own_take`)
- `send target, tag, move(payload)` — move payload into the mailbox (`fr_send_move`)

The compiler rejects use-after-move. See `examples/ownership.fg` and `docs/first.md` §6.


## Language Example

```forge
process main {
    coroutine worker(id: int) {
        println("start", id);
        yield;
        println("done", id);
    }

    spawn worker(1);
    spawn worker(2);
}
```

## Standard Modules

Import modules with `import`. Standard modules are included in `libforge_std`.

| Module | Description |
|--------|-------------|
| `io` | `print`, `print_int`, `read_line`, `read_char`, `read_stdin`, `prompt`, `flush`, `write_fd`, `read_fd`, `eprint`, `eprintln` |
| `strings` | `str_len`, `str_concat`, `str_eq`, … |
| `math` | `abs_i`, `min_i`, `max_i`, `pow_i`, … |
| `time` | `time_now_ms`, `sleep_ms` |
| `fs` | `fs_read`, `fs_write`, `fs_append`, `fs_exists`, `fs_remove`, `fs_size`, `fs_is_file`, `fs_is_dir`, `fs_mkdir`, `fs_rename`, `fs_copy`, `fs_list_dir` |
| `os` | `os_exit`, `os_getenv`, `os_argc`, `os_argv` |
| `tcp` | `tcp_listen`, `tcp_connect`, `tcp_send`, `tcp_recv` |
| `udp` | `udp_bind`, `udp_send`, `udp_recv` |
| `http` | `http_get`, `http_listen`, `http_prepare`, `http_serve_mt`, … |
| `event` | `event_poll`, `event_add_read` (epoll integration) |
| `json` | `json_get_string`, `json_get_int`, … |
| `gpu` | `gpu_available`, `gpu_alloc`, `gpu_add_i32`, `gpu_mul_i32`, `gpu_run_kernel`, … (OpenCL) |

```forge
import gpu;

native main {
    if (gpu_available() == 0) { return 0; }
    gpu_select_device(0);
    let buf: int = gpu_alloc(4096);
    gpu_fill_i32(buf, 42, 1024);
    gpu_free(buf);
}
```

```forge
import io;
import tcp;

process main {
    let server: int = tcp_listen(8080);
    println("listening");
}
```

## User Libraries

### Define a Library

```forge
library greeting {
    import strings;

    export fn hello(name: string): string {
        return str_concat("Hello, ", name);
    }
}
```

### Compile

```bash
./build/bin/forge --lib libs/greeting/greeting.fg \
    -o greeting.c --header greeting.h
```

### Use

```forge
import greeting;

process main {
    println(greeting.hello("Forge"));
}
```

With CMake, use `forge_add_library()` from `cmake/ForgeLibrary.cmake`.

## Language Server (LSP)

Editor support for `.fg` files — syntax highlighting, diagnostics, completion, hover, and document symbols.

See [docs/lsp.md](docs/lsp.md) for setup. Quick start:

```bash
cmake --build build
cd lsp && npm install && npm run build
cd ../editors/vscode && npm install && npm run build
```

Then install the extension from `editors/vscode/` in VS Code or Cursor.

## Project Structure

```
forge/
├── compiler/       # Forge compiler (lexer, parser, codegen)
├── runtime/        # M:N scheduler, epoll event loop, ownership, mailbox
├── stdlib/         # standard module C implementations
├── include/        # runtime and stdlib headers
├── libs/           # sample user libraries
├── examples/       # example programs
├── benchmark/      # Forge vs Python vs Phoenix vs Rust Axum HTTP benchmark
│   ├── forge/      # Forge benchmark server (.fg)
│   ├── python/     # Python benchmark server (.py)
│   ├── phoenix/    # Phoenix (Bandit) benchmark server
│   └── axum/       # Rust Axum benchmark server
├── cmake/          # CMake helpers
├── lsp/            # TypeScript language server
├── editors/vscode/ # VS Code / Cursor extension
└── docs/           # design documents and tutorials
```

## Compiler Usage

```bash
# Compile directly to a native executable
forge app.fg -o app --forge-root . --lib-dir build/lib

# Emit C source only (debugging)
forge app.fg -o app.c --emit-c

# Build a static library (.a + .h)
forge --lib lib.fg -o libforge_mylib.a --header mylib.h \
    --forge-root . --lib-dir build/lib
```

## Contributing

Issues and pull requests are welcome. Please read **[CONTRIBUTING.md](CONTRIBUTING.md)** for PR rules, review expectations, and our AI-friendly contribution policy before opening a PR.

1. Fork the repository and create a branch
2. Commit your changes
3. Open a pull request against `main`

## License

[MIT License](LICENSE) — free to use, modify, and distribute.

## Design Docs

For the execution model and design goals, see [docs/first.md](docs/first.md).

## Browser output and distribution

`forge main.fg --emit-js -o main.js` emits JavaScript for functions, source modules,
enums, control flow and native main. Integers use BigInt and 64-bit arithmetic.
Extern functions resolve through `globalThis.ForgeNative`. Native processes,
coroutines, structs and linking archives are rejected in this mode. String
length/index use UTF-8 bytes; slicing must stay on valid UTF-8 boundaries in a
browser. The JavaScript backend does not supply ownership or memory safety.

The separate [Forge Platform](https://github.com/forge-language/forge-platform)
contains the React/TypeScript/Tailwind website, Forge registry backend, Forge
package manager and SHA-256-checked `curl | bash` installer. Preview releases
support Linux x86_64 with glibc 2.35+ and a C compiler.
[Forge Browser](https://github.com/forge-language/forge-browser) provides generic
DOM/HTTP primitives for Forge applications.

The [portfolio migration](https://github.com/Helloworld0822/portfolio-platform/pull/1)
uses Forge server and browser application code with independent PostgreSQL, web
and browser modules. Its performance report compares complete implementations
on one host; it is not a universal Forge-versus-Rust benchmark.
