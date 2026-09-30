---
layout: home

title: UVRPC — Ultra-Fast C99 RPC Framework
titleTemplate: false

hero:
  name: UVRPC
  text: Ultra-Fast C99 RPC Framework
  tagline: Zero threads. Zero locks. Zero mutable globals. ~61,000 req/s in-process.
  actions:
    - theme: brand
      text: Quick Start
      link: /quick-start
    - theme: alt
      text: GitHub
      link: https://github.com/adam-ikari/uvrpc

features:
  - title: 🚀 Ultra-Fast
    details: Built on the libuv event loop and FlatBuffers. SAMELOOP/INPROC transports deliver ~61,000 req/s at ~16 µs round-trip latency on a CI runner.
    link: /guide/benchmark
  - title: 🎯 Minimal by Design
    details: Zero threads, zero locks, zero file-scope globals in library code. All I/O is driven by a single libuv event loop — lock-free by construction.
    link: /guide/design-philosophy
  - title: 🔌 Five Transports, One API
    details: TCP, UDP, IPC, INPROC, and SAMELOOP — switch transports by changing the address prefix. Identical server/client API everywhere.
    link: /guide/api-guide
  - title: 📦 Zero-Copy
    details: FlatBuffers binary serialization with no intermediate copies. In-process transports pass pointers directly for minimum overhead.
  - title: 🔄 Loop Injection
    details: Bring your own uv_loop_t. Run multiple independent instances, or share a loop for zero-overhead same-loop calls.
  - title: 📚 Type-Safe Codegen
    details: Declare services with the FlatBuffers DSL; generate type-safe C client/server stubs with compile-time checking.
    link: /api/generated-api

---

::: tip What UVRPC is for
UVRPC is a **call-semantics** RPC framework — low-latency, small-payload request/response.
It is not a big-data transport; use object storage, streaming pipelines, or shared
memory for bulk data. In-process transports are already zero-copy pointer-passing,
optimal for small payloads.
:::

## Quick Start

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh          # build vendored libuv/flatcc/mimalloc/gtest
./build.sh                        # Release + mimalloc (default allocator)

# Run a TCP echo round-trip
./dist/bin/simple_server &
./dist/bin/simple_client
```

### Server

```c
#include "uvrpc.h"

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    uvrpc_request_free(req);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_config_t* cfg = uvrpc_config_new();
    uvrpc_config_set_loop(cfg, &loop);
    uvrpc_config_set_address(cfg, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_TCP);

    uvrpc_server_t* server = uvrpc_server_create(cfg);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);

    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_server_free(server);
    uvrpc_config_free(cfg);
    uv_loop_close(&loop);
    return 0;
}
```

### Client

```c
#include "uvrpc.h"

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    /* resp->result / resp->result_size hold the reply */
    uvrpc_response_free(resp);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_config_t* cfg = uvrpc_config_new();
    uvrpc_config_set_loop(cfg, &loop);
    uvrpc_config_set_address(cfg, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_TCP);

    uvrpc_client_t* client = uvrpc_client_create(cfg);
    uvrpc_client_connect(client);

    uint8_t msg[] = "hello";
    uvrpc_client_call(client, "echo", msg, sizeof(msg), on_response, NULL);

    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_client_free(client);
    uvrpc_config_free(cfg);
    uv_loop_close(&loop);
    return 0;
}
```

## Performance

Sequential ping-pong (one request in flight), 8-byte payload, Release build,
single thread, measured by the [`Benchmark` workflow](https://github.com/adam-ikari/uvrpc/actions/workflows/benchmark.yml)
on a GitHub Actions runner. Absolute numbers vary widely by host — see
[Benchmark](/guide/benchmark) for methodology.

| Transport | Round-trip latency | Throughput (1/latency) | Use case |
|-----------|-------------------|------------------------|----------|
| SAMELOOP  | 16.42 µs | ~61,000 req/s | Same-loop, vtable bypass |
| INPROC    | 16.25 µs | ~62,000 req/s | In-process zero-copy |
| IPC       | 24.55 µs | ~41,000 req/s | Local inter-process (Unix socket) |
| UDP       | 30.91 µs | ~32,000 req/s | Loss-tolerant; loopback-only figure |
| TCP       | 30.59 µs | ~33,000 req/s | Reliable network RPC |

::: warning Throughput definition
"Throughput" above is the reciprocal of sequential round-trip latency (one
request in flight), **not** pipelined throughput. Reproduce with
`./dist/bin/perf_benchmark [requests] [transport]`.
:::

## Design Philosophy

- **Zero threads** — all I/O is managed by the libuv event loop. No thread pools, no background workers.
- **Zero locks** — the single-threaded model makes mutexes, spinlocks, and atomics unnecessary.
- **Zero mutable globals** — library code has no file-scope mutable globals (system/mimalloc builds); all state lives in context objects.

[Read the full design philosophy →](/guide/design-philosophy)

## Transports

| Transport | Address | When to use |
|-----------|---------|-------------|
| TCP       | `tcp://host:port` | Reliable network RPC |
| UDP       | `udp://host:port` | High-throughput, loss-tolerant |
| IPC       | `ipc:///path`     | Local inter-process |
| INPROC    | `inproc://name`   | In-process zero-copy; needs a registry |
| SAMELOOP  | `sameloop://name` | Same loop, vtable bypass; needs a registry |

Switching transports is a one-line change — only the address prefix differs.

## Dependencies

- **libuv** (≥ 1.0) — event loop
- **FlatCC** — FlatBuffers compiler/runtime
- **uthash** — hash tables
- **mimalloc** (optional) — high-performance allocator
- **GoogleTest** (tests only)

## Next Steps

- [Quick Start](/quick-start) — 5-minute tutorial
- [Build & Install](/build-install) — full build instructions
- [API Guide](/guide/api-guide) — complete API documentation
- [Benchmark](/guide/benchmark) — methodology and numbers
- [Single Thread Model](/guide/single-thread-model) — why no locks

UVRPC is MIT-licensed. [Star it on GitHub →](https://github.com/adam-ikari/uvrpc)
