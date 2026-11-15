# Forge language specification — current preview

This is an implementation-oriented preview specification, not a stability or memory-safety guarantee. Source files use .fg and statements generally end with semicolons. The strategy document's fn main(), spawn blocks, range loops and simplified CLI examples describe possible future UX, not the current syntax.

## First program

~~~forge
fn square(value: int): int {
    return value * value;
}
native main {
    println("Hello, Forge!");
    println(square(12));
    return 0;
}
~~~

Compile with forge main.fg -o hello; execute ./hello. Native compilation emits C internally and invokes a C compiler. --emit-c preserves C; --emit-js -o main.js selects experimental JavaScript output.

## Declarations and control flow

Functions use fn name(argument: type): return_type. The compiler supports top-level functions, recursion and forward declarations. Bindings use let name: type = expression; constants use const NAME = expression;. Supported runtime values include int, float, bool and string, with additional composite declarations in the native compiler.

Control flow includes if / else, while, C-style for, return, break, continue and integer match with a wildcard _ arm. Pipe syntax value |> function() passes value as the first argument. Check examples/ for authoritative executable examples.

int is a signed 64-bit integer in native output. Native signed overflow follows the generated C/toolchain behavior and must not be treated as a safety guarantee. JavaScript output uses BigInt and explicit 64-bit wrapping; division guards are implemented in its runtime. Do not assume identical overflow behavior between backends.

## Modules and libraries

import strings; exposes standard string functions such as str_len and str_concat. Source modules can define exports; library declarations generate native libraries. -I supplies source import paths and -l selects archives. Standard module names and available functions are listed in README.md.

## Processes and coroutines

~~~forge
process main {
    coroutine worker(id: int) {
        println("start", id);
        yield;
        println("done", id);
    }
    spawn worker(1);
    spawn worker(2);
}
~~~

A process owns local state and coroutine execution. Processes execute on a multithreaded scheduler; coroutines yield cooperatively. send / recv provide mailbox operations. await waits for descriptor readiness from a coroutine. Scheduling order is not an API promise. Blocking native operations can block workers.

## Ownership

own let marks owned heap strings, move(value) transfers ownership and send target, tag, move(payload) moves a message into a mailbox. Code generation detects some use-after-move cases. This is not a complete borrow checker, lifetime system or proof against use-after-free, double-free and data races. --check does not perform complete type/ownership verification.

## Browser subset

Supported: functions, enums, source modules, native main, basic control flow and supported standard builtins. Unsupported: processes, coroutines, structs and native archive linking. Externs require a ForgeNative bridge. String length/index are UTF-8 bytes; slices must stay on UTF-8 boundaries.

The website Playground supplies a single source file and built-in JS APIs only. It does not supply arbitrary modules, networking or filesystem bridges.

## Self-hosted subset

bootstrap/compiler.fg supports integer/boolean/string/void functions, typed initialized bindings, integer match, loops, standalone blocks and selected io/strings/fs/os builtins. Boolean values use stage0's C int representation; true/false become 1/0, and logical operators retain short-circuit evaluation. println accepts string variables, function results, constants and multiple arguments; arguments are evaluated once from left to right without inserted separators. It accepts native main and sequential process main. Unsupported modules, coroutines, floats, arrays and malformed delimiters are rejected. See docs/compiler-and-selfhosting.md for exact commands and boundaries.

## Compatibility

This preview may change syntax, ABI and module APIs. Pin exact module versions and Git commits in forge.lock. A grammar/type-system stabilization RFC is a roadmap task; the parser and regression examples are authoritative when prose is incomplete.
