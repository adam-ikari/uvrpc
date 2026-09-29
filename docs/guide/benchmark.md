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
transport=inproc    requests=100000  received=100000  elapsed=246 ms  (sequential ping-pong)
  round-trip latency: 2.46 us/req
  throughput (= 1/latency, 1 in-flight): 406504 req/s
```

That is a real line from a Linux container on a 12-core Ryzen 7 5800H, quiet at
the time of measurement. Run the same binary while something else is compiling
and INPROC reads ~23 µs/req instead of ~2.5 µs — an order of magnitude from
contention alone. Numbers also differ across hosts for reasons the design does
not control. This is why the table below is labelled as one machine's figures
rather than a specification, and why any single run must say what it ran on.

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
round-trip actually cost you. On the container above: direct 0.256 ns, function
pointer 0.271 ns, indirect-with-checks 0.305 ns — dispatch overhead is two
orders of magnitude below one round trip, so the cost of an in-process call is
framing and queue turn, not the pointer hop.

## Reference numbers

Release build, `-O2`, system allocator, single thread, 8-byte payload, measured
on the author's development machine:

| Transport | Round-trip latency | Throughput (1/latency) | Use case |
|---|---|---|---|
| SAMELOOP | ~4.9 µs | ~205,000 req/s | same-loop, vtable bypass (fastest) |
| INPROC | ~4.9 µs | ~205,000 req/s | in-process zero-copy |
| IPC | ~31 µs | ~33,000 req/s | local inter-process (Unix socket) |
| UDP | ~38 µs | ~26,000 req/s | loss-tolerant |
| TCP | ~46 µs | ~22,000 req/s | reliable network RPC |

For comparison, the same build on the container described above measured
~2.4 µs (SAMELOOP), ~2.5 µs (INPROC), ~21 µs (IPC), ~25 µs (UDP), ~32 µs (TCP)
— faster than the table on the in-process transports and on the socket ones.
Absolute numbers are host-dependent; the *ratios* (in-process ≫ local socket ≫
network) are what the design actually guarantees. Treat the table as a
regression baseline recorded on one host, not a capacity promise. UDP in sequential
ping-pong also carries real packet-loss risk; a lost response shows up as a
timeout error, not as degraded throughput.

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
