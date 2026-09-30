# Quick Start

Get UVRPC built and run your first RPC in 5 minutes.

## Prerequisites

- GCC ≥ 4.8 (or Clang)
- CMake ≥ 3.15
- make

## Build

The vendored submodules (libuv, flatcc, mimalloc, gtest) must be built before
configure — `cmake` alone cannot find flatcc.

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh          # clone submodules, build libuv/flatcc/mimalloc/gtest
./build.sh                        # cmake -S . -B build + build (Release, mimalloc)
```

Binaries are output to `dist/bin/`.

::: tip Allocator
The default build uses mimalloc, which `setup_deps.sh` builds from the submodule.
To use the system allocator instead: `cmake -S . -B build -DUVRPC_ALLOCATOR_DEFAULT=system`
(then build as usual). This only switches the allocator — flatcc still comes from
`setup_deps.sh`.
:::

## Run the example

```bash
# Terminal 1 — start the server
./dist/bin/simple_server

# Terminal 2 — run the client
./dist/bin/simple_client
```

You should see a request/response round-trip. Congratulations — that's UVRPC.

## Core concepts

### Transports

UVRPC supports five transports, selected by address prefix, with an identical
API across all of them:

| Transport  | Address              | Use case                       |
|------------|----------------------|--------------------------------|
| TCP        | `tcp://host:port`    | Reliable network RPC           |
| UDP        | `udp://host:port`    | High-throughput, loss-tolerant |
| IPC        | `ipc:///path`        | Local inter-process            |
| INPROC     | `inproc://name`      | In-process zero-copy; needs a registry |
| SAMELOOP   | `sameloop://name`    | Same loop, vtable bypass; needs a registry |

### Configuration

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVBUS_TRANSPORT_TCP);
```

Switching between the three socket transports is a one-line change — only the
address prefix and `UVBUS_TRANSPORT_*` constant differ. INPROC and SAMELOOP are
the exception: they meet through a registry the caller creates and passes to
every config that has to see the others.

```c
uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
uvrpc_config_set_loop_registry(server_config, reg);
uvrpc_config_set_loop_registry(client_config, reg);
/* ... after the servers and clients are gone: */
uvbus_loop_registry_free(reg);
```

## A complete RPC

### Server

```c
#include "uvrpc.h"

void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    int32_t a = *(int32_t*)req->params;
    int32_t b = *(int32_t*)(req->params + 4);
    int32_t result = a + b;
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
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
    uvrpc_server_register(server, "add", add_handler, NULL);
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
    int32_t result = *(int32_t*)resp->result;
    printf("Result: %d\n", result);
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

    int32_t params[2] = {10, 20};
    uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params),
                      on_response, NULL);

    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_client_free(client);
    uvrpc_config_free(cfg);
    uv_loop_close(&loop);
    return 0;
}
```

## RPC modes

UVRPC supports three call modes on the client:

```c
/* Normal request/response */
uvrpc_client_call(client, "add", params, size, on_response, ctx);

/* Oneway — fire-and-forget, no response */
uvrpc_client_call_oneway(client, "log", params, size);

/* Streaming response — server sends multiple chunks */
uvrpc_client_call(client, "stream", params, size, on_stream_response, ctx);
```

On the server, use `uvrpc_request_send_response_more` for intermediate chunks
and `uvrpc_request_send_response` for the final one.

## Error handling

```c
int ret = uvrpc_server_start(server);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Failed to start server: %d\n", ret);
    return 1;
}
```

In a response callback, check `resp->status`:

```c
static void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status != UVRPC_OK) {
        fprintf(stderr, "Request failed: %d\n", resp->status);
    }
    uvrpc_response_free(resp);
}
```

## Performance

Sequential ping-pong, 8-byte payload, Release build, single thread, measured
by the `Benchmark` workflow on a GitHub Actions runner. Absolute numbers are
host-dependent — see [Benchmark](/guide/benchmark) for the full table.

| Transport | Relative speed |
|-----------|----------------|
| SAMELOOP / INPROC | fastest |
| IPC       | ~1.5x slower |
| UDP       | slowest (loopback only) |
| TCP       | slowest |

No absolute figures: two GitHub Actions runners of the same image differed by
2.2x on identical code, so a number quoted without its host is not a property of
the library. The `Benchmark` workflow publishes the median of five 100k-request
runs together with the hardware it measured on.

::: warning
"Throughput" above is the reciprocal of sequential round-trip latency (one
request in flight), **not** pipelined throughput.
:::

## Next steps

- [Build & Install](/build-install) — full build options and dependencies.
- [API Guide](/guide/api-guide) — complete API documentation.
- [Benchmark](/guide/benchmark) — methodology and numbers.
- [Design Philosophy](/guide/design-philosophy) — zero threads, zero locks, zero mutable globals.
- [Examples](https://github.com/adam-ikari/uvrpc/tree/main/examples) — runnable example programs.
