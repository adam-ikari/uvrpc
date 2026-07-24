# Guide

UVRPC is a minimal, high-performance C99 RPC framework built on libuv and
FlatBuffers. This guide walks through using it end-to-end.

## Sections

- [Quick Start](/quick-start) — build UVRPC and run your first RPC in 5 minutes.
- [Build & Install](/build-install) — dependencies, CMake options, and installation.
- [API Guide](/guide/api-guide) — the server/client API, transports, and RPC modes.
- [Benchmark](/guide/benchmark) — performance methodology and measured numbers.
- [Design Philosophy](/guide/design-philosophy) — zero threads, zero locks, zero globals.
- [Single Thread Model](/guide/single-thread-model) — why a single-threaded event loop needs no locks.

## The 30-second tour

```c
/* Server: register a handler and run the loop */
uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);

/* Client: connect and call */
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
uvrpc_client_call(client, "echo", data, size, on_response, NULL);
```

Transports are selected by address prefix — `tcp://`, `udp://`, `ipc://`,
`inproc://`, `sameloop://` — with an identical API across all of them.

## What UVRPC is (and isn't)

UVRPC targets **call semantics**: low-latency, small-payload request/response.
It is not a bulk-data transport — use object storage, streaming pipelines, or
shared memory for large payloads. In-process transports are already zero-copy
pointer-passing, optimal for small payloads.
