# Checked builder-byte inline fast path

Same-session measurement from `2026-10-10T07:45:03Z` to `2026-10-10T07:46:20Z`. Each workload has 18 randomized, order-balanced triples of baseline, candidate and Rust, with checked same-input prewarming. Compiler/runtime and workload/timer source hashes are identical. The baseline retains checked inline views; the candidate additionally inlines capacity-backed builder byte writes.

| Workload | Baseline / Rust | Candidate / Rust (95% interval) | Candidate speedup (95% interval) | 1.10 target |
| --- | ---: | --- | --- | --- |
| lcg | 1.0095 | 1.0098 [1.0089, 1.0110] | 1.0000 [0.9995, 1.0008] | below target |
| primes | 1.0107 | 1.0112 [1.0104, 1.0122] | 1.0004 [0.9999, 1.0014] | below target |
| scan | 0.9661 | 0.9667 [0.9450, 0.9686] | 0.9985 [0.9622, 1.0028] | below target |
| builder | 0.6367 | 0.9571 [0.9489, 0.9606] | 1.5059 [1.4897, 1.5090] | below target |
| immutable | 0.2297 | 0.2301 [0.2290, 0.2317] | 1.0016 [0.9995, 1.0034] | below target |

Every correctness, prewarm and measured checksum matches an independent Python reference. All samples remain in the raw report. Confidence intervals describe these paired ratios on this host and are not simultaneous confidence over all workloads. The original and earlier different-date view-inline reports remain preserved separately.

The fast path preserves exported builder-char symbols and the existing data/length/capacity layout. Invalid bytes and null handles fail, capacity subtraction avoids overflow, and growth delegates to the external ABI. Views/builders still require trusted live arena handles; this is not a general memory-safety guarantee. Snapshots remain immutable copies. Rust and Forge retain their documented allocation lifetime differences.

Finite CPU kernels do not establish language-wide 110% throughput, HTTP/application performance, safe vector support, or cross-machine portability. Whole-process RSS is a diagnostic and can include the forked runner before exec.

Raw samples and source/artifact hashes: [builder-byte-inline-2026-10-10.json](builder-byte-inline-2026-10-10.json).

The native stdlib archive is also byte-for-byte identical between the snapshots.
This experiment isolates installed-header caller inlining using preserved
compiler/runtime binaries, and does not measure concurrent release compiler or
runtime changes. Builder throughput improves by 50.6% over the view-inline
baseline in the paired session, while remaining approximately 95.7% of Rust
throughput. All five target intervals remain below 1.10. Host timing outliers
are retained without selective exclusions.
