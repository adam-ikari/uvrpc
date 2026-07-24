# UVRPC Benchmarks

Performance measurement tools for UVRPC.

## Programs

### perf_benchmark

Per-transport request/response round-trip latency and throughput.

**Usage:**
```bash
./dist/bin/perf_benchmark [requests] [transport]
```

- `requests` — number of round-trips (default: `100000`)
- `transport` — `sameloop` | `inproc` | `ipc` | `udp` | `tcp` (default: `inproc`)

**Methodology:** strict sequential ping-pong (one request in flight at a time —
waits for each response before sending the next). The drain loop uses
`uv_run(UV_RUN_ONCE)` (realistic blocking-wait, not busy-poll). Single-threaded:
server and client share one `uv_loop_t`. 8-byte echo payload.

**Output:** round-trip latency (µs/req) and throughput (req/s). Note that
"throughput" reported is the reciprocal of sequential round-trip latency
(one request in flight), **not** pipelined throughput.

**Examples:**
```bash
# Quick run, default (inproc, 100k requests)
./dist/bin/perf_benchmark

# 100k requests over TCP
./dist/bin/perf_benchmark 100000 tcp

# 1M-request stress run on SAMELOOP
./dist/bin/perf_benchmark 1000000 sameloop
```

**Important:** build with `-DUVRPC_DEBUG_LOGGING=OFF` for valid timings — debug
logging writes to stderr on every send/recv and corrupts measurements. The
binary prints a warning if built with logging on.

### direct_call_benchmark

Measures raw function-call overhead with no transport, serialization, or
networking. Establishes a baseline to contextualize the SAMELOOP/INPROC
overhead measured by `perf_benchmark`.

```bash
./dist/bin/direct_call_benchmark
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUVRPC_DEBUG_LOGGING=OFF
cmake --build build --target perf_benchmark direct_call_benchmark
```

## Reference numbers

Release build, `-O2`, system allocator, single thread, 8-byte payload. See
`docs/guide/benchmark.md` and the project README for current numbers. Run the
benchmark on your own hardware for representative results.
