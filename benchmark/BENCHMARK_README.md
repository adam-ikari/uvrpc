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

### grpc_ping_pong

C++ gRPC side of the comparison with UVRPC. Synchronous unary echo over
loopback TCP, one request in flight, 8-byte payload, 1000 warmup requests
excluded — the same methodology as `perf_benchmark tcp`. Output uses the same
three lines, with `transport=grpc_tcp`.

```bash
./dist/bin/grpc_ping_pong [requests] [port]   # defaults: 100000, 16557
```

gRPC core uses its own threads and the payload carries protobuf plus gRPC/HTTP2
framing, so this is comparable to UVRPC's TCP transport, not INPROC/SAMELOOP.
The gRPC C++ toolchain is not part of a normal build; the target is gated behind
`-DUVRPC_GRPC_BENCHMARK=ON`.

### uvrpc_sync_ping_pong

UVRPC measured with gRPC's call style: `uvrpc_client_call_async()` blocks the
calling thread and drives the shared loop inline, the counterpart of gRPC's
synchronous unary client. Same 8-byte echo, one request in flight, 1000 warmup,
sequential ping-pong. Kept separate from `perf_benchmark` so the callback-pump
baseline and its CI gate stay stable.

```bash
./dist/bin/uvrpc_sync_ping_pong [requests]   # default 100000, TCP port 16558
```

The async block's deadline covers the whole run, so it is derived from the
measured warmup rate (a fixed 5 s block expires partway through 100k calls).

### compare_grpc.sh

Runs both sides on one host with matched methodology (5 samples by default,
100k requests each), prints host details, medians, spreads, raw samples and peak
RSS (`VmHWM`), and warns when a sample spread exceeds 2.0x. A contended run with
a spread above that is not a measurement and must not be quoted.

The `Benchmark` workflow runs this script and writes its table to the job
summary as an **informational comparison, not a gate**. No CI step fails on
its contents. `benchmark/check_grpc_comparison.py` is a local diagnostic for
checking data quality by hand; it is not called by CI.

```bash
./benchmark/compare_grpc.sh [requests] [samples]   # defaults: 100000, 5
```

### CI transport gate

`benchmark/check_baseline.py` checks the five `perf_benchmark` transports
against `benchmark/baseline.json`. It gates only **within-family ratios**:
INPROC/SAMELOOP, IPC/TCP and UDP/TCP, tolerance 50%. Cross-family ratios
(SAMELOOP/TCP) track the runner hardware and are printed but not gated. A
previously unseen CPU is recorded under `benchmark/cpu-records/` and never
blocks CI on its hardware signature. Missing transports, spread above 2.0x or a
global absolute collapse still fail the gate.

```bash
python3 benchmark/check_baseline.py --results benchmark_results.md \
  --baseline benchmark/baseline.json --records benchmark/cpu-records
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUVRPC_DEBUG_LOGGING=OFF
cmake --build build --target perf_benchmark direct_call_benchmark
```

For the gRPC comparison, install the toolchain and add the gated target:

```bash
sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc protobuf-compiler libprotobuf-dev
cmake -S . -B build-grpc -DCMAKE_BUILD_TYPE=Release \
  -DUVRPC_ALLOCATOR_DEFAULT=system -DUVRPC_DEBUG_LOGGING=OFF \
  -DUVRPC_GRPC_BENCHMARK=ON
cmake --build build-grpc --target perf_benchmark grpc_ping_pong
```

## Reference numbers

All baselines come from the `Benchmark` GitHub Actions workflow, which samples
5 x 100k requests with the system allocator and logging OFF, checks transport
ratios and the gRPC comparison, and records the runner. `ci.yml` has no second
performance job. The current baseline table and its run ID are in
`docs/guide/benchmark.md` and the project README.

Local runs are for diagnosis, not for baselines. A development host under load
can measure TCP at three times the CI runner's figure while the code is
identical; an A/B against the released binary is needed to tell a host effect
from a regression. Quote a number only with its run ID and host.
