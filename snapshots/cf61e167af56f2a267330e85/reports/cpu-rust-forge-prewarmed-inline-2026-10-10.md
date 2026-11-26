# Native Forge / Rust CPU comparison

Recorded at `2026-10-10T07:36:06Z`. Ratio = Rust elapsed / Forge elapsed for the same work; >=1.10 means at least 110% of Rust throughput on this case.

| Workload | n × rounds | Forge median ms | Rust median ms | Paired ratio median | Bootstrap 95% interval | 1.10 threshold |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| lcg | 250000 × 64 | 59.931 | 60.587 | 1.0106 | [1.0082, 1.0117] | below target |
| primes | 2000 × 128 | 74.153 | 74.830 | 1.0106 | [1.0099, 1.0114] | below target |
| scan | 131072 × 512 | 95.544 | 92.273 | 0.9662 | [0.9649, 0.9681] | below target |
| builder | 32768 × 1024 | 153.706 | 98.178 | 0.6373 | [0.6339, 0.6381] | below target |
| immutable | 4096 × 512 | 396.454 | 91.405 | 0.2304 | [0.2287, 0.2382] | below target |

Protocol: `prewarmed-balanced`. Per-input warmups, when enabled, are checked and retained separately; the prewarmed-balanced protocol makes each language run first in exactly half the measured pairs. These results must not be merged with the original protocol without qualification.

Each measured output matches an independent Python reference (modular exponentiation, sieve of Eratosthenes, or periodic weighted-sum formula). No checksum failure is excluded or silently retried.

Both executables use identical runtime n/rounds/seed arguments and a common C monotonic-clock primitive plus one-shot input/output optimization barriers. There is no C implementation of a measured algorithm. The report preserves every paired sample, warmup, calibration choice, source/artifact hash, compiler command and environment detail.

Builds use GCC -O3 and Rust opt-level=3, native CPU tuning and explicit LTO settings. Forge native runtime/stdlib archives are taken from the specified existing Release build; their non-LTO C functions may remain out of line, while Rust standard-library methods can inline. This comparison measures these implementations and supported public APIs rather than isolating a language syntax cost.

String builder and immutable append create the same ASCII bytes and final weighted checksum. Rust builder output includes a final clone to match Forge's immutable snapshot. Forge arena allocation retains intermediate strings until the explicit reset; Rust drops obsolete owned strings earlier. Cleanup is inside the timed rounds for builder/immutable, outside timing for the prebuilt scan input. The process RSS includes setup, allocator retention and runtime startup.

This small CPU suite does not cover typed arrays/vectors (no supported safe typed vector API in the tested Forge frontend; raw pointer indexing exists), concurrency, HTTP, database I/O, safety guarantees, large applications or cross-machine portability. Shared host services and frequency scheduling remain sources of noise despite CPU affinity and paired randomized order. A bootstrap interval describes this run, not universal performance. Passing one row cannot establish a language-wide 110% guarantee.

The repeated seeds are runtime inputs and output-dependent arithmetic prevents replacing the work with a printed constant. Timed instructions include mode dispatch and a few common observer calls; process startup, command parsing, output formatting and prebuilt scan input construction are excluded. Full process wall/CPU/RSS are separate diagnostics.

The wait4 RSS diagnostic can include the forked Python runner footprint before exec and is not a precise language heap measurement. The separate [controlled comparison note](string-view-inline-2026-10-10.md) records source/ABI isolation and the different-date limitation.
