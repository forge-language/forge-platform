# Scheduler completion notifications — 2026-10-09

The worker completion path now wakes `fr_scheduler_run()` only when both
`active_coros` and `native_pending` reach zero. The predicate is checked under the
same scheduler mutex used by the run loop. I/O registration still signals that
condition, and stop still broadcasts. Runnable-queue publication, coroutine
exclusive execution, readiness-before-return handling, and mailbox wakeups are
unchanged.

This reduces unnecessary run-loop wakeups in a workload of many short tasks. It
is a runtime before/after comparison, **not a Rust comparison or a language-wide
performance claim**. The global scheduler mutex and queue mutexes remain; more
workers still take longer than one worker in this workload.

## Measurement

On an AMD BC-250, 12 logical CPUs, Ubuntu GCC 13.3.0, both variants use
`-std=c11 -O3 -D_GNU_SOURCE` and pthreads. The baseline is runtime
`39ab3daa90f15852cbbf4dd97f5d4c1502bd392d`; the candidate is the uncommitted change
identified by its SHA-256 in the raw data. The benchmark is
`benchmark/scheduler_bench.c` at benchmarks `2ac8735973c3f46a482cbace3fd763e1419a6493`.

Each execution runs 100,000 coroutines in one process. Each coroutine yields
once and then increments a completion counter. Spawn and destruction are outside
the wall/process CPU timing; worker start and stop are inside. A Forge yield can
be resumed within its reduction budget, so this does not model a queue switch on
every yield. All measured executions completed exactly 100,000 tasks.

There are two excluded warmups for each variant/worker count, then 15 pairs for
1/2/4 workers. Before/after order and worker-count order alternate by trial.
Uninstrumented executables supply the timing table. Compiler builds, tests and
other agent benchmarks were paused during the timing window. This remains a
shared host with other services, rather than an isolated performance machine.

| Workers | Before wall median ms | After wall median ms | Before CPU median ms | After CPU median ms | Paired speedup median | Paired median bootstrap 95% interval |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 33.724 | 19.961 | 54.342 | 18.940 | 1.887× | 1.529–2.396× |
| 2 | 58.502 | 38.048 | 129.330 | 66.745 | 1.569× | 1.429–1.750× |
| 4 | 110.098 | 71.045 | 393.106 | 227.571 | 1.577× | 1.341–1.764× |

The paired speedup is the median of each trial's before/after wall-time ratio;
it is not the ratio of the two independently calculated medians. The interval
resamples the 15 observed paired ratios 10,000 times with seed 20261009 and reports
the percentile interval for their median. It describes uncertainty within these
observations; it does not guarantee the same improvement on another machine.

| Workers | Before wall range ms | After wall range ms |
| --- | --- | --- |
| 1 | 19.218–48.933 | 7.209–27.384 |
| 2 | 32.069–74.930 | 13.941–53.688 |
| 4 | 74.191–125.997 | 50.129–80.740 |

The trials are short and noisy, especially with one worker. The consistent paired
improvement supports this narrowly scoped change, but these data do not justify
extrapolating to long-running CPU tasks, HTTP servers, or arbitrary concurrency.
The previous portfolio HTTP comparisons use libmicrohttpd and libpq instead of
this coroutine scheduler.

## Run-loop evidence

Separate diagnostic executables wrap `fr_cond_wait` at link time and count calls
from the main thread. Their main-thread CPU clock covers only the
`fr_scheduler_run` invocation. Five alternating diagnostic pairs per worker count
produce the following medians. These instrumented times are not mixed into the
uninstrumented timing table above.

| Workers | Before main condition waits | After main condition waits | Before main CPU ms | After main CPU ms |
| --- | ---: | ---: | ---: | ---: |
| 1 | 4,307 | 1 | 30.116 | 1.901 |
| 2 | 6,095 | 1 | 35.222 | 1.874 |
| 4 | 11,765 | 1 | 71.855 | 2.057 |

The old loop was not a busy spin: it repeatedly woke, acquired the scheduler
mutex, rechecked a still-false completion predicate, and waited again. Signals
can coalesce, so these wait counts are not counts of every worker completion.
The candidate suppresses those intermediate completion notifications. The
reduced main-thread work supports the proposed explanation without attributing
all timing changes to one instruction or claiming that global lock contention
has been eliminated.

## Correctness checks

Release CTest passes all three runtime suites. AddressSanitizer and
UndefinedBehaviorSanitizer also pass all three suites. Existing tests cover
immediate/delayed await readiness, readiness racing an executing coroutine,
native completion, concurrent submission, receive wakeup, stop/restart, indexed
nested work, terminal error accounting and failed I/O registration.

The new mixed-work regression combines 1,000 short coroutines, eight native
jobs and six pipe reads, with one and four workers and early/delayed writers. It
requires the run loop to notice I/O registration while the batch drains, finish
both native and coroutine work, preserve exclusive reader execution, and return
correctly on a repeated run. It asserts external outcomes rather than the
number of condition signals.

## Reproduction and raw records

[Raw timing samples, diagnostic samples, source/binary SHA-256 values and the
measurement script](scheduler-completion-notify-2026-10-09.json) are preserved.
The raw file embeds the diagnostic C sources and the exact Python measurement
script. Before and after runtime source hashes differ only for `scheduler.c`.

For each runtime source checkout, build the plain benchmark with:

```sh
cc -std=c11 -O3 -D_GNU_SOURCE \
  -I "$RUNTIME/include" -I "$RUNTIME/src" \
  benchmark/scheduler_bench.c "$RUNTIME"/src/*.c \
  -lpthread -o scheduler-bench
./scheduler-bench throughput 100000 1
./scheduler-bench throughput 100000 2
./scheduler-bench throughput 100000 4
```

For the diagnostics, replace the benchmark with the embedded `probe-bench.c`,
add `wait-probe.c`, and link with `-Wl,--wrap=fr_cond_wait`. The measurement script
expects before/after binaries beneath a task-specific temporary directory; adapt
its `base` path when reproducing. Preserve the original report when collecting
new observations.
