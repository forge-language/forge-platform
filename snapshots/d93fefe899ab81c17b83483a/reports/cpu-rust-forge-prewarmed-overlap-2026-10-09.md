# Native Forge / Rust CPU comparison

Recorded at `2026-10-09T17:13:22Z`. Ratio = Rust elapsed / Forge elapsed for the same work; >=1.10 means at least 110% of Rust throughput on this case.

| Workload | n × rounds | Forge median ms | Rust median ms | Paired ratio median | Bootstrap 95% interval | 1.10 threshold |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| lcg | 250000 × 64 | 59.957 | 60.553 | 1.0089 | [1.0078, 1.0117] | below target |
| primes | 2000 × 128 | 74.002 | 74.945 | 1.0115 | [1.0111, 1.0125] | below target |
| scan | 131072 × 512 | 118.002 | 93.149 | 0.7898 | [0.7808, 0.7963] | below target |
| builder | 32768 × 1024 | 178.535 | 98.125 | 0.5494 | [0.5480, 0.5519] | below target |
| immutable | 4096 × 512 | 398.866 | 91.832 | 0.2309 | [0.2286, 0.2351] | below target |

Protocol: `prewarmed-balanced`. Per-input warmups, when enabled, are checked and retained separately; the prewarmed-balanced protocol makes each language run first in exactly half the measured pairs. These results must not be merged with the original protocol without qualification.

Each measured output matches an independent Python reference (modular exponentiation, sieve of Eratosthenes, or periodic weighted-sum formula). No checksum failure is excluded or silently retried.

Both executables use identical runtime n/rounds/seed arguments and a common C monotonic-clock primitive plus one-shot input/output optimization barriers. There is no C implementation of a measured algorithm. The report preserves every paired sample, warmup, calibration choice, source/artifact hash, compiler command and environment detail.

Builds use GCC -O3 and Rust opt-level=3, native CPU tuning and explicit LTO settings. Forge native runtime/stdlib archives are taken from the specified existing Release build; their non-LTO C functions may remain out of line, while Rust standard-library methods can inline. This comparison measures these implementations and supported public APIs rather than isolating a language syntax cost.

String builder and immutable append create the same ASCII bytes and final weighted checksum. Rust builder output includes a final clone to match Forge's immutable snapshot. Forge arena allocation retains intermediate strings until the explicit reset; Rust drops obsolete owned strings earlier. Cleanup is inside the timed rounds for builder/immutable, outside timing for the prebuilt scan input. The process RSS includes setup, allocator retention and runtime startup.

This small CPU suite does not cover typed arrays/vectors (no supported safe typed vector API in the tested Forge frontend; raw pointer indexing exists), concurrency, HTTP, database I/O, safety guarantees, large applications or cross-machine portability. Shared host services and frequency scheduling remain sources of noise despite CPU affinity and paired randomized order. A bootstrap interval describes this run, not universal performance. Passing one row cannot establish a language-wide 110% guarantee.

The repeated seeds are runtime inputs and output-dependent arithmetic prevents replacing the work with a printed constant. Timed instructions include mode dispatch and a few common observer calls; process startup, command parsing, output formatting and prebuilt scan input construction are excluded. Full process wall/CPU/RSS are separate diagnostics.

This run overlapped two concurrent Ninja builds reported by another worker. It is excluded in entirety from the controlled optimization comparison; raw samples remain preserved.
