# Checked inline string view comparison

Baseline: `2026-10-09T17:14:45Z`. Inline callers: `2026-10-10T07:36:06Z`. Both use balanced 16 paired repetitions and a checked same-input prewarm immediately before every measured binary, with identical n/rounds and random seeds. The original protocol report remains separate.

| Workload | Baseline throughput / Rust | Inline throughput / Rust | Inline 95% interval | Ratio change |
| --- | ---: | ---: | --- | ---: |
| lcg | 1.0101 | 1.0106 | [1.0082, 1.0117] | 1.001× |
| primes | 1.0122 | 1.0106 | [1.0099, 1.0114] | 0.998× |
| scan | 0.7843 | 0.9662 | [0.9649, 0.9681] | 1.232× |
| builder | 0.5490 | 0.6373 | [0.6339, 0.6381] | 1.161× |
| immutable | 0.2293 | 0.2304 | [0.2287, 0.2382] | 1.005× |

The native compiler, runtime archive and stdlib archive have identical SHA256 hashes in both snapshots. The rebuilt stdlib archive is byte-for-byte identical to the baseline archive: the implementation change is exposing the existing checked view reads through a shared installed inline header when compiling callers. The header preserves the original pointer/size_t layout and exported function ABI; function-pointer and opt-out callers retain external functions. Each byte still has null/negative/out-of-range checks and unsigned-byte semantics.

Scan improves by approximately 23% relative to the baseline throughput ratio; builder improves by approximately 16% because its final checksum scan uses the same view accessors. Integer loops and immutable concatenation show no comparable change. Every measured and warmup checksum passed, and none of the five rows has a 95% lower bound at or above 1.10. The Rust 110% objective is not achieved for this suite.

The runs straddle an interrupted session and were recorded on different dates. They are separate experiments rather than alternating before/after pairs, so the ratio change is descriptive and has no paired causal confidence interval. Within each run, balanced ordering and prewarming reduce the original prime workload order effect. Host services and frequency still limit conclusions. A separate overlapped run is preserved but excluded in entirety after another worker reported concurrent builds.

wait4 peak RSS is a whole-process diagnostic and can include setup, allocator retention, runtime startup and the forked Python runner footprint before exec. It does not establish language heap usage or ownership safety. These CPU rows do not cover safe typed vectors, concurrency, HTTP, database I/O or other machines.

Raw baseline: [cpu-rust-forge-prewarmed-baseline-2026-10-09.json](cpu-rust-forge-prewarmed-baseline-2026-10-09.json). Raw inline: [cpu-rust-forge-prewarmed-inline-2026-10-10.json](cpu-rust-forge-prewarmed-inline-2026-10-10.json). Raw overlap: [cpu-rust-forge-prewarmed-overlap-2026-10-09.json](cpu-rust-forge-prewarmed-overlap-2026-10-09.json).
