# Native OS thread comparison — 2026-10-10

This extends coverage to public native thread creation/join APIs. It does not
measure Forge coroutines, mailbox queues, HTTP, or a Rust async executor.
Throughput ratio is Rust elapsed / Forge elapsed; the target is at least 1.10.

| Physical cores / threads | Forge median ms | Rust median ms | Paired ratio median | Bootstrap 95% interval |
|---|---:|---:|---:|---:|
| 1 | 243.117 | 244.388 | 1.0055 | 1.0036–1.0105 |
| 2 | 122.403 | 126.805 | 1.0104 | 0.9915–1.0533 |
| 4 | 103.623 | 103.957 | 1.0304 | 0.8783–1.1715 |

No row establishes the 110% goal. The four-core interval is wide; crossing 1.10
in its upper endpoint is not evidence that the target has been achieved.
Intervals resample the observed paired ratios, rather than divide independent
medians, and describe this shared-host run only.

Both implementations partition 16,000,000 LCG iterations per round among the
requested workers and run four rounds. Each partition starts with seed + worker
index. Partition checksums can differ across worker counts, but match between
languages for each case. The total iteration count is fixed; these results do
not establish scaling of an application with a fixed observable result.

Forge uses `thread_spawn_indexed` and `thread_join_all` in `native main`, which
creates native OS threads without an active scheduler pool. Rust uses
`std::thread::spawn` and joins every handle. Thread creation/join, per-worker
argument parsing and final common counter reads are inside the timer. Forge's
thread registry costs and Rust's argument-vector allocation are implementation
costs, not identical internal mechanisms. A common C bridge provides the clock,
one-shot optimization barriers and two atomic completion/checksum counters; all
LCG arithmetic runs in Forge or Rust. No algorithm is supplied by C FFI.

Every executable result is checked against independent Python modular
exponentiation, and both binaries require exactly the requested completion count.
There are 18 small checks including partitions with zero work, followed by 16
balanced, randomized pairs for each worker count. Each sample has a checked
identical-input prewarm. All 192 measured/prewarm executions and all small checks
passed. Each child is restricted to one logical CPU from each selected physical
core: [0], [0, 2], or [0, 2, 4]. Build/test work was held during measurement.

The SDK is an immutable copy of the original CPU baseline snapshot, **not the
latest compiler/stdlib candidate**. Compiler/runtime/stdlib archive hashes match
the [original CPU report](cpu-rust-forge-2026-10-09.json). Header/source/binary
hashes and exact compiler commands are in the [raw thread report](native-thread-rust-forge-2026-10-10.json).
GCC uses O3/native CPU/LTO for generated C; Rust uses opt-level 3/native CPU/thin
LTO/one codegen unit/overflow checks off. LCG inputs keep intermediate products
within int64; no signed overflow is required to obtain these results.

## Reproduce

Create an immutable SDK snapshot with the CPU harness, then use a fresh output
path and build directory:

```sh
python3 benchmark/cpu_compare.py --build-only --build-dir build-cpu-snapshot
python3 benchmark/thread_compare.py \
  --snapshot build-cpu-snapshot/snapshot \
  --build-dir build-thread-comparison \
  --output docs/native-thread-new.json
```

The thread runner refuses existing output files. It records per-process CPU/RSS
separately from kernel elapsed time. Shared services, CPU frequency, scheduling
and memory topology remain limits; this is not a cross-machine guarantee or a
language-wide conclusion.
