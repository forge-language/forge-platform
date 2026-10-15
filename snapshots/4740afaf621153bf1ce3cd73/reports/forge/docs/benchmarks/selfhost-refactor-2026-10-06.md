# FG compiler refactoring verification — 2026-10-06

The FG-written compiler retains the existing subset → C → native toolchain
pipeline. Paired-delimiter scanning and semicolon scanning now share their
respective implementations. Block bindings and for-loop initializers append
through one statement emitter directly into the caller's string builder.
Expression tokens outside builtin prefixes are copied from the source view
without allocating an intermediate arena substring.

An expression line comment previously swallowed punctuation appended by the
C emitter, causing a native compiler error. Expression emission now replaces
each line comment with whitespace, including string literal arguments to
`println`. Literal contents and escaped quotes remain intact.

Verification for this refactoring snapshot, before the subsequent typed-output changes:

- Release build and `forge-selfhost-verify` pass (stage2/stage3 C output fixed point).
- All 14 CTest suites pass, including a new stage0/stage2 execution comparison
  for comments in bindings, returns, conditions, print arguments and for headers.
- An actual `cmake --install` prefix was moved to a path containing spaces;
  both installed compilers built and ran a native program from another directory
  with `FORGE_ROOT` unset.

## Local measurement

Baseline: `9b1d1a6c86a688ba56158bd5c014d836c151f289:bootstrap/compiler.fg`.
Both baseline stage2 and current stage2 used GCC 13.3.0, `-std=c11 -O3`,
the same build SDK and static archives. Input was the identical 57,614-byte
baseline compiler source. Only C emission was timed, including process startup
and temporary-file writes. Three warmups preceded 101 measured runs per compiler;
order alternated, with no concurrent project test/build jobs.

Median wall time: baseline **17.555 ms**, current **16.719 ms** (4.8% lower in
this run). Shared-host noise and the short workload limit this observation;
it does not establish a general compiler or generated-program speedup.
All samples, platform details and input/compiler hashes are in
[the raw JSON](selfhost-refactor-2026-10-06.json).

To reproduce after building the current SDK:

```bash
mkdir -p build/selfhost-bench
git show 9b1d1a6c86a688ba56158bd5c014d836c151f289:bootstrap/compiler.fg > build/selfhost-bench/baseline.fg
build/bin/forge build/selfhost-bench/baseline.fg -o build/selfhost-bench/stage1 --forge-root build --lib-dir build/lib --cc /usr/bin/cc
build/selfhost-bench/stage1 build/selfhost-bench/baseline.fg -o build/selfhost-bench/baseline.c
cc -std=c11 -O3 -Ibuild/include build/selfhost-bench/baseline.c -o build/selfhost-bench/baseline -Lbuild/lib -lforge_std -lforge_runtime -lm -pthread
python3 tests/selfhost_bench.py --baseline build/selfhost-bench/baseline --current build/bin/forge-stage2 --source build/selfhost-bench/baseline.fg --runs 101 --output build/selfhost-refactor-benchmark.json
```

Full type/ownership checking, arbitrary modules, arrays, float/bool support and
coroutine compilation remain outside the documented self-hosted subset. This
verification does not complete those broader language goals.

## Subsequent typed-output changes

The self-hosted compiler now uses the same C11 `_Generic` printer selection
already present in stage0 for unknown external return types. The old list of
string-returning function spellings has been removed. `println` now handles
string variables, parameters, constants, user function results and mixed
arguments; each argument executes once in source order. Comma splitting shares
the bounded separator scanner with statement parsing and ignores comments,
strings and nested parentheses. Empty comma arguments are rejected before
writing C output. Standalone blocks retain their C lexical scope.

Differential execution exposed a stage0 local-type bug: shadowing changed the
outer binding's remembered type after leaving the block. Native code generation
now appends local bindings to a stack, resolves the innermost binding first and
restores the stack at block/loop/branch exits. Explicit blocks emit C braces.
Regressions cover parameter and constant shadowing, branches, loops, match arms,
standalone blocks, mixed print arguments and side-effect evaluation order.
The final typed-output code passes all 14 CTest suites and the bootstrap fixed
point. Its generated C also compiles with `-std=c11 -pedantic-errors`, and both
installed compilers execute the mixed string/integer shadowing example after
their installation prefix is relocated with `FORGE_ROOT` unset.
The benchmark numbers above belong to the earlier binary hashes in the JSON;
they do not measure these subsequent changes.

## Boolean bootstrap support

The FG compiler supports `bool` parameters, return types, initialized bindings
and `true`/`false` literals using stage0's C `int` representation. Type acceptance
is shared between function and binding validation. Boolean literal conversion
uses whole-token boundaries and leaves string contents and names such as
`true_value` intact. Constant expressions pass through the expression emitter
and are parenthesized, correcting both boolean macro output and arithmetic
precedence (`const BASE = 1 + 2; BASE * 3` now yields 9).

Predicate functions and string-scanner state inside the compiler itself now use
boolean types and literals, exercising this support during self-compilation.
Differential execution regressions cover boolean parameters and returns, mutual
recursion, constants, negation/comparison, short-circuit side effects, loops,
sequential processes and literal/identifier boundaries. This remains a subset
compiler and does not add a complete type or ownership checker.

## Match statement boundaries — 2026-10-07

A multiline match arm reproduced a stage2 failure to terminate normally, while
stage0 executed both statements in the arm. The line-based arm reader has been
replaced with bounded pattern/statement scanners. Emission reads arm bodies
directly from ranges in the original source instead of truncating them at a
newline or allocating a separate source slice per arm. The scanner handles
comments, literals, nested blocks, conditionals (including else-if chains),
loops and nested matches. Branches on the same line and unbraced single-statement
arms retain the stage0 behavior; break/continue still act on the surrounding loop.

Validation runs before output creation. Non-integer patterns, integer patterns
above INT64_MAX, missing arrows or statement terminators, missing arm bodies and
non-final wildcard arms are rejected. Leading-zero patterns are emitted as
decimal integers rather than C octal literals. The statement boundary scanner
limits recursive else-if depth; this is not a complete resource-limit policy.

Verification includes differential native execution for multiline/inline/nested
arms, single scrutinee evaluation, loop control, decimal patterns and INT64_MAX;
negative cases check that no C output is created. All 14 CTest suites and the
self-hosting fixed point pass. Compiler integer-boundary checks are also run
with UBSan, and generated boolean/multiline-match C is checked with strict C11
and both relocated installed compilers.
