# Performance Testing

UVRPC ships two measurement programs. Both are single-threaded on purpose: the
framework has no threads, so a benchmark that spawns them would be measuring
something the library cannot do.

| Program | Source | Measures |
|---|---|---|
| `perf_benchmark` | [`benchmark/perf_benchmark.c`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/perf_benchmark.c) | per-transport request/response round-trip latency and throughput |
| `direct_call_benchmark` | [`benchmark/direct_call_benchmark.c`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/direct_call_benchmark.c) | raw function-call overhead — the floor under SAMELOOP/INPROC |

There is **no** multi-thread, multi-process, publisher/subscriber, or percentile
benchmark. Earlier documentation described a `benchmark` binary with `-t`,
`--fork`, `--publisher`, `--latency` and a p50/p95/p99 report. That program no
longer exists and its flags were never part of `perf_benchmark`.

## Building

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh
./build.sh release
```

Both binaries land in `dist/bin/`. `./build.sh` leaves `UVRPC_DEBUG_LOGGING` at
its default (`OFF`), which is what you want — see the guard below.

## perf_benchmark

```
./dist/bin/perf_benchmark [requests] [transport]
```

- `requests` — number of round-trips (default: `100000`)
- `transport` — `sameloop` | `inproc` | `ipc` | `udp` | `tcp` (default: `inproc`)

An unknown transport exits with status 2. Addresses are fixed per transport
(`benchmark/perf_benchmark.c:101-120`): `tcp://127.0.0.1:16555`,
`udp://127.0.0.1:16556`, `ipc:///tmp/uvrpc_perf.sock`, `inproc://uvrpc_perf`,
`sameloop://uvrpc_perf`. So only one TCP/IPC run can occupy its socket at a time.

```bash
./dist/bin/perf_benchmark                      # inproc, 100k
./dist/bin/perf_benchmark 100000 tcp           # TCP, 100k
./dist/bin/perf_benchmark 1000000 sameloop     # SAMELOOP stress run
```

Output:

```
transport=sameloop  requests=1000000  received=1000000  elapsed=974 ms  (sequential ping-pong)
  round-trip latency: 0.97 us/req
  throughput (= 1/latency, 1 in-flight): 1033058 req/s
```

That is a real run from the `Benchmark` workflow (run `36527761470`). Run the
same binary while something else is compiling and INPROC reads an order of
magnitude slower — contention alone does that. Numbers also differ across
hosts for reasons the design does not control. This is why the table below is
labelled as one runner's figures rather than a specification, and why any
single run must say what it ran on.

## What the numbers mean — and what they do not

**Throughput here is the reciprocal of sequential round-trip latency.** The
measured loop sends one request, pumps the loop until its response arrives, and
only then sends the next (`benchmark/perf_benchmark.c:190-213`). Exactly one
request is in flight. This is *not* pipelined or concurrent throughput, and the
program deliberately does not pretend otherwise: the output line says
`sequential ping-pong` and the throughput line says `= 1/latency, 1 in-flight`.

To measure async throughput you must pipeline calls yourself — issue N requests
without waiting and let the callback drain them. The limits you will hit are the
three lock-free rejections documented in
[Single Thread Model](/guide/single-thread-model).

### Methodology preconditions

Four things make these numbers comparable, and each is enforced in code rather
than left to the operator:

1. **Pump with `UV_RUN_ONCE`, not busy-polling.** `pump_until()`
   (`benchmark/perf_benchmark.c:59-79`) blocks the thread until an event is
   ready. A predecessor that used `UV_RUN_DEFAULT` hung, because a live
   connection never lets the loop exit.
2. **Debug logging must be OFF.** `UVRPC_DEBUG` / `UVBUS_DEBUG` write to stderr
   on every send and recv, which dwarfs the RPC cost. The binary refuses to
   produce quiet numbers: it prints `WARNING: library built with debug logging
   ON — timings are NOT valid` (`benchmark/perf_benchmark.c:89-92`). If you see
   it, rebuild with `-DUVRPC_DEBUG_LOGGING=OFF`.
3. **Warmup is excluded from timing.** `WARMUP_REQUESTS` (1000) requests run
   first (`benchmark/perf_benchmark.c:164-178`); the clock starts after them.
   Loop setup also gets explicit settle spins — 10 before the client connects,
   50 after (`:141`, `:150`).
4. **Deadlines come from the host, not from an assumed cost per request.** The
   measured run gets `4 × observed_warmup_rate × requests + 10 s`
   (`benchmark/perf_benchmark.c:184-188`), and it is only cut short early when
   no response arrives for `stall_limit_ms` (5 s by default) — a wedged
   transport, as opposed to a merely slow machine. The previous revision
   hard-coded `requests/10 + 10000` ms, which silently assumed round-trip never
   exceeds 0.1 ms/req; on a virtualized host TCP ran ~0.4 ms/req and a 50,000
   request run died with `measure timeout` having printed nothing. Force a
   budget with `UVRPC_BENCH_BUDGET_MS=<ms>` or change the stall window with
   `UVRPC_BENCH_STALL_MS=<ms>` when the defaults do not fit your machine.

Two more premises: server and client **share one `uv_loop_t`** — required by
INPROC and SAMELOOP, and TCP/IPC run on the shared loop too
(`benchmark/perf_benchmark.c:136-141`) — and the payload is **8 bytes**, so
these figures describe RPC framing cost, not bulk-data transfer.

## direct_call_benchmark

```bash
./dist/bin/direct_call_benchmark
```

Calls a function through a callback pointer with no transport, no
serialization, no libuv. Use it as the subtractive baseline: if a direct call
costs `X` and SAMELOOP costs `Y`, then `Y - X` is what the RPC framing and loop
round-trip actually cost you. Measured alongside the table below: direct
0.339 ns, function pointer 0.335 ns, indirect-with-checks 0.362 ns — dispatch
overhead is three orders of magnitude below one round trip, so the cost of an
in-process call is framing and queue turn, not the pointer hop.

## Reference numbers

Release build, `-O2`, system allocator, single thread, 8-byte payload, 50,000
round trips per transport, measured by the `Benchmark` workflow on a GitHub
Actions `ubuntu-latest` runner (run `36527761470`, commit `9c9aa24`):

### Why there is no table of absolute numbers here

An earlier version of this page carried a table of round-trip latencies with a
run ID in the caption, which read like a specification. It was not one. Two
GitHub Actions runners from the same image, running identical code, reported
35.84 µs and 16.42 µs for SAMELOOP — a 2.2x difference. Those tables were also
wrong on their own terms: they put SAMELOOP first as the fastest transport when
it measures level with INPROC, and separated TCP from UDP by 0.02 µs when the
two are indistinguishable.

A latency figure without the machine it came from is not a property of this
library, so none is published. What is stable across hosts is the ordering:

| Transport | Relative speed | Use case |
|---|---|---|
| SAMELOOP, INPROC | fastest | in-process, no syscall |
| IPC | ~1.5x slower | local inter-process (Unix socket) |
| TCP, UDP | slowest | network |

To get numbers you can use, trigger the `Benchmark` workflow. It samples each
transport five times, reports the median with the min-max spread, and records
the CPU, core count, memory, kernel, load average and frequency governor of
the host it measured on — in the run summary and in the job log. A number
quoted without that host should be treated as unverified. For capacity planning,
measure on the hardware you intend to deploy on.

The workflow then gates those medians with `benchmark/check_baseline.py` against
`benchmark/baseline.json`: all five transports must report, each sample spread
must stay within 2.0x, each transport's latency relative to SAMELOOP may not
move more than 50% from the baseline, and SAMELOOP itself may not exceed three
times the baseline. Ratios cancel runner-wide drift; absolute latency
does not. Update the baseline only when a code change is meant to shift the
relative cost of a transport. The deadline-sweep fix was such a change: the
baseline was re-recorded from run `45359431694` (EPYC 7763), where SAMELOOP fell
from 15.84 to 0.92 µs and IPC/UDP/TCP moved from roughly 1.5–2x to 12–22x that
reference. A few percent of cross-run drift alone is never a reason to update.

**The UDP row is a loopback figure and nothing more.** The benchmark is
sequential ping-pong with exactly one request in flight, and the library does
not retransmit: a dropped datagram means the response never arrives, the stall
detector fires, and the run aborts with `measure stalled`. It cannot show up as
a lower number, so a completed UDP run tells you the link dropped nothing --
it says nothing about a real network, where loss and latency both apply. The
program now prints that caveat to stderr before the UDP numbers, and the
figure is not comparable with the TCP row on anything but loopback.

## UVRPC vs gRPC

A runnable C++ gRPC harness measures the same workload as `perf_benchmark` so
the two can be compared on one host in one session.

### What is compared — and what is not

| | UVRPC side | gRPC side |
|---|---|---|
| Program | `perf_benchmark tcp` / `uvrpc_sync_ping_pong` | `grpc_ping_pong` |
| Call shape | callback pump, or blocking `uvrpc_client_call_async()` | synchronous unary call, wait for its response, next |
| Requests in flight | exactly one | exactly one |
| Payload | 8 raw bytes in an RPC frame | 8 bytes in a `bytes` field, plus protobuf and gRPC/HTTP2 framing |
| Transport | loopback TCP, port 16555 | loopback TCP, port 16557 |
| Warmup | 1000 requests, excluded | 1000 requests, excluded |
| Sampling | 5 runs of 100k, median | 5 runs of 100k, median |

Not compared: INPROC and SAMELOOP (gRPC has no in-process transport), pipelined
or concurrent throughput, TLS (both sides use plaintext loopback), or
multi-process / cross-machine calls. The structural differences are part of the
number, not hidden: gRPC core runs its own threads while UVRPC uses one libuv
loop, and gRPC carries protobuf plus HTTP2 framing that UVRPC does not. This is
a comparison of each library's default call path, not of pure serialization
cost.

### Running it

On Ubuntu 22.04 the toolchain is packaged:

```bash
sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc protobuf-compiler libprotobuf-dev

cmake -S . -B build-grpc \
  -DCMAKE_BUILD_TYPE=Release \
  -DUVRPC_ALLOCATOR_DEFAULT=system \
  -DUVRPC_DEBUG_LOGGING=OFF \
  -DUVRPC_GRPC_BENCHMARK=ON
cmake --build build-grpc -j$(nproc) --target perf_benchmark grpc_ping_pong

./benchmark/compare_grpc.sh 100000 5
```

`UVRPC_GRPC_BENCHMARK` defaults to `OFF`, so a normal build does not need a
gRPC toolchain. The gRPC C++ stubs are generated from
[`benchmark/grpc_proto/echo.proto`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/grpc_proto/echo.proto)
at build time; no generated code is committed. Source:
[`benchmark/grpc_ping_pong.cc`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/grpc_ping_pong.cc),
[`benchmark/compare_grpc.sh`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/compare_grpc.sh).

### Why the earlier conclusion was overturned

The first two sessions on this host (load 3.4–4.0) put both libraries at 65–75 µs
with no stable ordering, and that was the published conclusion. An instruction
profile of the same workload invalidated it: `sweep_expired_pending()` accounted
for **96.9% of all instructions**. It scanned the whole pending-callback ring —
65,536 slots by default — on every call to reclaim expired deadlines, even when
nothing had expired. Disabling the sweep in a throwaway build dropped the same
run from ~70 µs to ~24 µs.

The fix keeps a count of live deadlines and the earliest one on the client. The
full ring scan now runs only when a deadline has actually passed; inserting and
releasing a slot update the index in constant time. Timeout reclamation is
unchanged — see the verification note below. These are new numbers, not a
reinterpretation of the old ones.

### Measured after the fix

Host for all rows below: `sandbox-001`, Linux 6.17.13-2-pve, AMD Ryzen 7 5800H
(12 cores, 24 GiB), `performance` governor. Release `-O2`, system allocator,
debug logging OFF, gRPC C++ 1.30.2 / protobuf 3.12.4, 8-byte payload.

**Optimization A/B** — five interleaved pairs, old and new binaries from the
same source tree, 100k requests per run, load ~13.5:

| Pair | Before | After |
|---|---|---|
| 1 | 76.94 µs | 27.75 µs |
| 2 | 75.03 µs | 27.76 µs |
| 3 | 74.98 µs | 27.57 µs |
| 4 | 73.28 µs | 26.98 µs |
| 5 | 173.01 µs | 56.37 µs |

Medians 75.03 µs → 27.76 µs, a **63% reduction**. Pair 5 was a load spike on
both sides; the ratio still held.

**UVRPC vs gRPC after the fix** — five interleaved pairs in one session, load
~10.9:

| Side | Samples (µs) | Median | gRPC ÷ UVRPC |
|---|---|---|---|
| UVRPC TCP | 28.41 27.98 28.31 27.37 26.86 | 27.98 µs | — |
| gRPC sync unary | 73.92 72.84 71.48 72.04 71.94 | 72.04 µs | 2.57x |

### UVRPC tested with gRPC's call style

`uvrpc_sync_ping_pong` replaces the
callback pump with `uvrpc_client_call_async()`, which blocks the calling thread
and runs the shared loop inline. That is the UVRPC counterpart of gRPC's
synchronous unary client, so the two sides now use the same call shape. One
clean 5×100k session (load 3.9, all spreads ≤1.1x):

| Side | Samples (µs) | Median | Peak RSS | vs gRPC latency |
|---|---|---|---|---|
| UVRPC callback pump | 24.80 24.76 24.32 24.68 24.50 | 24.68 µs | 2,676 KiB | 2.99x faster |
| UVRPC blocking await | 24.84 24.45 25.53 25.88 26.30 | 25.53 µs | 2,972 KiB | 2.89x faster |
| gRPC sync unary | 73.29 72.28 73.74 75.18 75.09 | 73.74 µs | 11,684 KiB | — |

The blocking-await binary is separate on purpose: changing `perf_benchmark`'s
pump would invalidate the transport baseline and the CI gate built on it. Its
async block deadline is derived from the measured warmup rate — a fixed 5 s
block expires partway through a 100k-request run, which is why the first version
of this harness timed out. The extra ~300 KiB over the callback binary is the
async context and copied results, still 3.9x below gRPC.

**Memory**, peak RSS (`VmHWM`) of the whole process during a full run, gRPC's
worker threads included: UVRPC callback 2,676 KiB / blocking 2,972 KiB vs gRPC
11,684 KiB — 4.4x / 3.9x lower. This is after reducing the default pending ring
from 65,536 to 1,024 slots; a separate A/B found no latency difference (25.57 vs
26.21 µs median, within host noise) and 508 KiB less RSS. Reproduce with
`benchmark/compare_grpc.sh`, which prints all three rows, spreads, raw samples
and peak RSS, and warns when a spread exceeds 2.0x.

### CI gate

The `Benchmark` workflow installs the gRPC C++ toolchain, builds all three
binaries with the same Release/system-allocator/logging-OFF flags, runs
`benchmark/compare_grpc.sh 100000 5` and writes the table to the step summary.
`benchmark/check_grpc_comparison.py` then fails the job when any of the three
rows is missing, any sample spread exceeds 2.0x, either UVRPC median is above
gRPC's, or either UVRPC peak RSS is above gRPC's. It is a relative gate on one
runner, not a
published specification; a red gate from a >2.0x spread means the host was
contended and the run must be repeated, not that the library regressed.

**Evidence level: measured here, one host, interleaved sessions, not a CI
baseline.** The A/B and the UVRPC/gRPC comparison each alternated binaries
within one session, so host drift affects both sides. A separate contended run
(load 16, gRPC spread 3.6x) is *not* quoted here; that spread fails the same
2.0x rule the CI gate uses. Correctness checks: `call_timeout_test` reports 4
failures when the sweep is disabled (negative control), and 206/206 tests pass
with the fix. Re-run on the hardware you intend to deploy on; absolute numbers
are a property of this sandbox, which is virtualized and shared.

## Troubleshooting

```bash
lsof -i :16555                 # port already taken by another run
rm -f /tmp/uvrpc_perf.sock     # stale IPC socket (the program unlinks it too)
ps aux | grep perf_benchmark
```

If `received < requests`, the run hit a deadline or a transport error and exited
non-zero — the numbers were never printed. Do not compare partial runs.

## Related Documentation

- [Design Philosophy](/guide/design-philosophy)
- [Single Thread Model](/guide/single-thread-model)
- [Architecture](/architecture/)
