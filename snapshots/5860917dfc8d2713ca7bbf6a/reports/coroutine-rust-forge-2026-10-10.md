# Complete-lifecycle coroutine comparison

Forge coroutines versus Tokio 1.53.0, measured from observer/runtime creation through spawning, all task completion and runtime shutdown. Every task ID appears exactly once and every final value matches independent Python modular exponentiation. Printing and argument parsing are outside timing.

| Case | Physical workers | Forge median ms | Tokio median ms | Throughput ratio (95% interval) | 1.10 target |
| --- | ---: | ---: | ---: | --- | --- |
| scheduler-heavy | 1 | 4.989 | 32.105 | 6.5192 [5.9716, 6.6447] | supported for this configuration |
| scheduler-heavy | 2 | 6.189 | 23.293 | 3.9470 [3.3861, 5.7225] | supported for this configuration |
| scheduler-heavy | 4 | 13.114 | 26.031 | 2.0505 [1.6312, 2.4619] | supported for this configuration |
| mixed-cpu | 1 | 252.083 | 269.709 | 1.0715 [1.0681, 1.0737] | below target |
| mixed-cpu | 2 | 140.176 | 138.969 | 1.0466 [0.9623, 1.0735] | below target |
| mixed-cpu | 4 | 100.224 | 95.847 | 1.0013 [0.8570, 1.1034] | inconclusive |
| budget-crossing | 1 | 6.845 | 56.451 | 8.2695 [8.0065, 8.3071] | supported for this configuration |
| budget-crossing | 2 | 4.928 | 42.276 | 7.4436 [6.9745, 9.0832] | supported for this configuration |
| budget-crossing | 4 | 6.872 | 47.964 | 7.3909 [4.7458, 8.2864] | supported for this configuration |

Ratio = Tokio elapsed / Forge elapsed for the same logical work. The ratio column is the median of within-pair ratios, which can differ from dividing the displayed independent medians. Each configuration has 16 order-balanced paired repetitions with randomized dynamic seeds and checked same-input prewarming. No checksum failure or timing outlier is excluded.

Both runtimes use 1/2/4 OS worker threads pinned to the same distinct physical cores. Both spawning drivers run on the main thread. Forge queues tasks in generated main_init before scheduler_run starts its workers; Tokio starts workers during runtime creation, before block_on spawns tasks. The exact generated-C adapter changes only worker count and lifecycle timer boundaries; original/adapted C and hashes are preserved. C provides config, timer and observer storage only; recurrence and logical yields live in FG/Rust.

Explicit yields are not equal context switches: Forge can continue within its reduction budget before requeue, while Tokio yield_now has its own cooperative scheduling and [non-guarantees](https://docs.rs/tokio/1.53.0/tokio/task/fn.yield_now.html). Worker configuration uses the [Tokio builder](https://docs.rs/tokio/1.53.0/tokio/runtime/struct.Builder.html). These measure supported scheduler policies and their complete lifecycle, not equivalent dispatch counts or a language-wide performance guarantee.

The scheduler-heavy case has 4,000 tasks × 128 yield rounds × 1 recurrence step. Mixed CPU has 4,000 tasks × 64 yield rounds × 256 steps. Budget crossing has 256 tasks × 4,096 yield rounds × 1 step, exceeding the observed Forge reduction budget of 2,000; the first two cases fit within that budget. Integer products remain bounded below signed 64-bit overflow. Results do not cover mailbox throughput, HTTP, I/O readiness or other machines. Shared-host services/frequency and runtime task-allocation differences remain relevant. RSS is a whole-process diagnostic, potentially including the forked Python runner footprint.

Raw samples, source/adapter/lock hashes and flags: [coroutine-rust-forge-2026-10-10.json](coroutine-rust-forge-2026-10-10.json).

Six scheduler-oriented configuration intervals exceed 1.10. None of the mixed
CPU configurations establishes that threshold: one and two workers are below it,
and four workers are inconclusive. Scheduler-heavy Forge throughput decreases
as workers increase in this run, so the results also expose scaling overhead.
The scheduler advantage measures the supported reduction-budget and spawn
startup policies; it does not establish equivalent context-switch costs or a
whole-language advantage over Rust.

The measured SDK was the clean installed `/tmp/forge-sdk-release-20261010`:
compiler content checkpoint `78faaf1`, runtime `ddeba40`, and stdlib `bd89481`.
Sibling source-state observations can include edits made after that build and
do not identify the contents of the compiler binary. The recorded compiler,
archive and header hashes identify the actual inputs used.

Reproduce with a validated SDK of that provenance and fresh output paths:

```sh
python3 benchmark/coroutine_compare.py \
  --sdk /tmp/forge-sdk-release-20261010 \
  --build-dir build-coroutine-reproduction \
  --output docs/coroutine-rust-forge-reproduction.json
```

The runner generates original C, validates exact adapter anchors and preserves
both files in the immutable local build snapshot. This run's snapshot is
`build-coroutine-validated-20261010/snapshot/`. The raw JSON records their hashes,
the executed measurement-runner source and its hash; later documentation edits
to the runner do not change the recorded measurement version.

Original C SHA256: `6d095d1af14334c55896858b83e5b7e9d7bfaea1c1aa2e7e5ec181248a02ecff`.

Adapted C SHA256: `82c88ff53c41a2944ecc85005b8e8692affa36c6c5cd7a45c1125e2ad0080b22`.
