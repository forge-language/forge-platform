# Parser allocation refactor — 2026-10-06

Top-level declaration, library and supervisor arrays now grow geometrically through
one checked allocation helper. Public AST layout and declaration order are preserved.

GCC 13.3, `-std=c11 -O3`; same generated input with 100,000 extern declarations,
15 parses per sample and seven alternating before/after samples:

| Metric | Before | After |
| --- | ---: | ---: |
| Median parse-only time per sample | 0.674668 s | 0.628612 s |
| Sample range | 0.643855–0.704939 s | 0.603605–0.636396 s |
| realloc calls per parse | 100,000 | 15 |

Median time decreased 6.83% (1.073× throughput). This measures parsing allocation
work; it is not a whole-program compiler or HTTP throughput claim. The harness and
comparison script live in forge-benchmarks. Reproduce against `forge@725aefb` and
this refactor revision using the same C compiler, machine and generated input.

Regression coverage checks all declaration categories, library imports/functions,
supervisor children and capacity boundaries. The regression passes with assertions,
AddressSanitizer and UndefinedBehaviorSanitizer.
