# Forge optimization measurements (2026-10-01)

Baseline: `9a6109c`. Raw observations and conditions are in
[optimization-performance.json](optimization-performance.json). This shared host
also runs other applications; small differences are not causal proof.

| Workload | Before | After | Interpretation |
|---|---:|---:|---|
| Bootstrap stage2 translates the same 36,440-byte source | 178.654 ms | 31.144 ms | 5.74×, identical C output; C compilation excluded, 7 alternating runs |
| 16 KiB byte scan | 5.547 ms | 0.0519 ms | Cached byte view instead of repeated strlen, 7 runs |
| 16 KiB byte append | 83.029 ms | 0.0664 ms | Builder avoids quadratic copying/arena retention, 7 runs |
| Two workers waiting on a pipe for 500 ms, process CPU | 1005.656 ms | 3.101 ms | 99.69% less CPU while idle, 5 alternating runs |
| Same pipe wait, wall clock | 501.447 ms | 500.688 ms | No material completion-latency penalty in this workload |
| Warm run-queue push/pop | 22.63 ns | 15.57 ns | 1.45×, bounded node reuse |
| Warm native-queue push/pop | 23.03 ns | 15.97 ns | 1.44× |
| Bulk run-queue steal | 13.40 ns | 14.12 ns | About 5.4% slower; no throughput improvement claimed |
| Bulk native-queue steal | 13.99 ns | 14.29 ns | About 2.1% slower |

The queue node cache retains at most 256 nodes per queue; peak backlog does not
permanently retain unlimited nodes. Owner FIFO and tail stealing are preserved.
Scheduler state publication and condition predicates share a lock, native work
stays pending until completion, and event polling releases the scheduler lock.
Pending I/O uses bounded polling; jobs without pending I/O wait on state changes.

String views borrow immutable bytes until arena reset. Builder finish returns an
independent immutable snapshot. The JavaScript backend follows NUL-terminated
UTF-8 byte semantics and rejects a finished builder containing invalid UTF-8.
These handles do not add static lifetime/type safety to the language.

`benchmark/string_bench.c`, `benchmark/scheduler_bench.c`, and
`benchmark/work_queue_bench.c` provide the native workloads. Build their targets
with Release CMake and keep assertions enabled for correctness checks.
Bootstrap fixed-point verification and CTest regressions were also run.

Compiler module merging now sizes declaration arrays once per imported module.
This reduces repeated realloc calls without claiming a measured compiler speedup
or changing the remaining parser/AST allocation strategy.

The portfolio API uses libmicrohttpd workers and synchronous libpq, not the Forge
coroutine scheduler. Its separate before/after/Rust report is in
`portfolio-platform/docs/forge-optimization-performance.md`. The Forge Web
module separately records HTTP-buffer/ownership/connection-reuse microbenchmarks;
those results do not predict application requests per second.
