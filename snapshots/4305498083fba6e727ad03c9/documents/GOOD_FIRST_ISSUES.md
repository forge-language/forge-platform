# First contribution candidates

These are ready-to-triage drafts, not claims that live GitHub issues exist. Maintainers should confirm scope and duplicates before publishing 10–20 good first issue entries. Each should explain expected behavior, relevant files and how to verify it.

| Title | Area / files | Acceptance / verification |
|---|---|---|
| Document native and JS overflow differences | LANGUAGE_SPEC.md, docs/examples | Runnable bounded arithmetic example; explain native C vs BigInt behavior |
| Add a beginner guide to exact module versions | forge-platform docs/api.md, README.md | Explain x.y.z, forge.lock and update behavior using supported commands |
| Add a source-location diagnostic example | docs/examples, tests | Show real malformed input and current stderr without promising better errors |
| Document UTF-8 byte length and slicing | examples, docs/examples | Compile and run Korean/ASCII example; keep slices on byte boundaries |
| Document cooperative yield in a long loop | examples, docs/examples | Explain yield requirement with a small bounded native example |
| Add a regression for a short builtin-like name | tests/selfhost_test.py | Verify user identifier does not map to a builtin; fixed point still passes |
| Document native dependencies per official module | forge-platform README.md | Commands verified on supported Ubuntu distribution |
| Explain benchmark warmup and shared-host limits | benchmark, docs | Link real commands/raw data; no unsupported speed claims |
| Improve registry empty-search keyboard flow | forge-platform frontend | Accessible empty result and retry; browser navigation check |
| Add compiler-option missing-value coverage | tests | CLI rejects a missing -o/-I argument; preserves input files |
| Document a small enum JavaScript example | examples, docs/examples | Compile with --emit-js and run through Node; record unsupported cases |
| Check internal documentation links | scripts, docs | Detect broken local Markdown targets without accessing secrets/build trees |

Small tasks should not require implementing a borrow checker, changing scheduler synchronization or redesigning the compiler. AI-assisted work is welcome; contributors must understand changes and record verification.
