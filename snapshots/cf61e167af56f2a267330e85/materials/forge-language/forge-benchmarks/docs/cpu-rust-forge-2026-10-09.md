# Native Forge / Rust CPU comparison

Recorded at `2026-10-09T16:59:41Z`. Ratio = Rust elapsed / Forge elapsed for the same work; >=1.10 means at least 110% of Rust throughput on this case.

| Workload | n × rounds | Forge median ms | Rust median ms | Paired ratio median | Bootstrap 95% interval | 1.10 threshold |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| lcg | 250000 × 64 | 59.924 | 60.470 | 1.0091 | [1.0074, 1.0136] | below target |
| primes | 2000 × 128 | 74.809 | 75.359 | 1.0130 | [0.7468, 1.3844] | inconclusive |
| scan | 131072 × 512 | 119.652 | 94.051 | 0.7859 | [0.7452, 0.8219] | below target |
| builder | 32768 × 1024 | 178.116 | 97.717 | 0.5497 | [0.5371, 0.5584] | below target |
| immutable | 4096 × 512 | 398.509 | 93.157 | 0.2340 | [0.2281, 0.2632] | below target |

Each measured output matches an independent Python reference (modular exponentiation, sieve of Eratosthenes, or periodic weighted-sum formula). No checksum failure is excluded or silently retried.

Both executables use identical runtime n/rounds/seed arguments and a common C monotonic-clock primitive plus one-shot input/output optimization barriers. There is no C implementation of a measured algorithm. The report preserves every paired sample, warmup, calibration choice, source/artifact hash, compiler command and environment detail.

Builds use GCC -O3 and Rust opt-level=3, native CPU tuning and explicit LTO settings. Forge native runtime/stdlib archives are taken from the specified existing Release build; their non-LTO C functions may remain out of line, while Rust standard-library methods can inline. This comparison measures these implementations and supported public APIs rather than isolating a language syntax cost.

String builder and immutable append create the same ASCII bytes and final weighted checksum. Rust builder output includes a final clone to match Forge's immutable snapshot. Forge arena allocation retains intermediate strings until the explicit reset; Rust drops obsolete owned strings earlier. Cleanup is inside the timed rounds for builder/immutable, outside timing for the prebuilt scan input. The process RSS includes setup, allocator retention and runtime startup.

This small CPU suite does not cover typed arrays/vectors (not currently available in the tested Forge frontend), concurrency, HTTP, database I/O, safety guarantees, large applications or cross-machine portability. Shared host services and frequency scheduling remain sources of noise despite CPU affinity and paired randomized order. A bootstrap interval describes this run, not universal performance. Passing one row cannot establish a language-wide 110% guarantee.

The repeated seeds are runtime inputs and output-dependent arithmetic prevents replacing the work with a printed constant. Timed instructions include mode dispatch and a few common observer calls; process startup, command parsing, output formatting and prebuilt scan input construction are excluded. Full process wall/CPU/RSS are separate diagnostics.

The `primes` case has a strong execution-order effect in these samples: the first process in a pair usually took about 97–104 ms and the second about 72–75 ms, regardless of language. Per-process CPU time changed with elapsed time as well, so this is not explained solely by being descheduled. Frequency/cache state after the Python reference calculation is a plausible cause, but was not isolated. Randomized order avoids always assigning the first position to one language; it does not remove this effect. Its wide interval therefore remains inconclusive. A later protocol may add per-input prewarming and balanced randomized order, and must use a separate report rather than replace this baseline.

Artifact hashes identify the actual tested compiler and archives. Recorded source-tree state was observed at capture time and includes in-progress runtime edits; it is not a claim that every existing archive was rebuilt from that observed dirty tree. The CPU binaries in `build-cpu-comparison` are immutable snapshots, so later runtime/compiler rebuilds cannot change this report.
