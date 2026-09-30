# API Reference

The UVRPC public API, declared in [`include/uvrpc.h`](https://github.com/adam-ikari/uvrpc/blob/main/include/uvrpc.h) and [`include/uvbus.h`](https://github.com/adam-ikari/uvrpc/blob/main/include/uvbus.h).

## Configuration

### `uvrpc_config_t`

Opaque configuration struct used to create servers and clients.

```c
typedef struct uvrpc_config uvrpc_config_t;
```

#### `uvrpc_config_new()`

Create a new configuration object with defaults.

```c
uvrpc_config_t* uvrpc_config_new(void);
```

#### `uvrpc_config_free()`

Free a configuration object.

```c
void uvrpc_config_free(uvrpc_config_t* config);
```

#### `uvrpc_config_set_loop()`

Set the libuv event loop. Required.

```c
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
```

#### `uvrpc_config_set_address()`

Set the transport address, e.g. `tcp://127.0.0.1:5555`, `inproc://name`. Required.

```c
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);
```

#### `uvrpc_config_set_transport()`

Set the transport type. Required.

```c
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvbus_transport_type_t transport);
```

#### `uvrpc_config_set_loop_registry()`

Set the registry the INPROC or SAMELOOP transport belongs to. Required for those
two, ignored by the socket transports.

Those transports keep no state of their own: a server and a client are built
from independent configurations and meet through this registry. Create one with
`uvbus_loop_registry_new()`, pass the same pointer to every configuration that
has to see the others, and free it with `uvbus_loop_registry_free()` once the
servers and clients are gone.

```c
uvrpc_config_t* uvrpc_config_set_loop_registry(uvrpc_config_t* config,
                                               uvbus_loop_registry_t* registry);
```

```c
uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
uvrpc_config_set_loop_registry(scfg, reg);
uvrpc_config_set_loop_registry(ccfg, reg);
/* ... */
uvbus_loop_registry_free(reg);
```

Leaving it unset for INPROC or SAMELOOP is an error, reported when the transport
is created. The framework keeps no hidden per-loop or process-wide state to
fall back on, so two unrelated endpoints cannot collide by accident.

#### `uvrpc_config_set_timeout()` / `uvrpc_client_set_timeout()`

Set how long a request may go unanswered before the application is told. The
config setter applies to every client built from that config; the client setter
changes it at runtime. `0` disables it.

```c
uvrpc_config_t* uvrpc_config_set_timeout(uvrpc_config_t* config, int timeout_ms);
int uvrpc_client_set_timeout(uvrpc_client_t* client, int timeout_ms);
```

```c
uvrpc_config_t* cfg = uvrpc_config_set_timeout(uvrpc_config_new(), 2000);
```

An expired request invokes its callback with status and error code
`UVRPC_ERROR_TIMEOUT`. The report is delivered no later than the next request
issued on that client — the sweep runs where a callback slot is about to be
taken, and there is no per-client timer, so an idle client is not interrupted.

This is what keeps a lost datagram from consuming a slot forever: without a
deadline, a response that never arrives holds its slot until the client is
disconnected, and the only symptom is `UVRPC_ERROR_RATE_LIMITED` long after the
cause. Note that a timeout cannot tell you *why* there was no response — loss,
a slow server, a crashed server and a wrong address look identical.

Transport types (`include/uvbus.h`):

| Constant | Value | Transport |
|----------|-------|-----------|
| `UVBUS_TRANSPORT_TCP` | 0 | TCP |
| `UVBUS_TRANSPORT_UDP` | 1 | UDP |
| `UVBUS_TRANSPORT_IPC` | 2 | Unix domain socket |
| `UVBUS_TRANSPORT_INPROC` | 3 | In-process (same process) |
| `UVBUS_TRANSPORT_SAMELOOP` | 4 | Same-loop, vtable bypass |

#### Tuning setters

All return the config pointer for chaining.

```c
uvrpc_config_t* uvrpc_config_set_max_clients(uvrpc_config_t* config, int max_clients);
uvrpc_config_t* uvrpc_config_set_max_concurrent(uvrpc_config_t* config, int max_concurrent);
uvrpc_config_t* uvrpc_config_set_max_pending_callbacks(uvrpc_config_t* config, int max_pending);
uvrpc_config_t* uvrpc_config_set_msgid_offset(uvrpc_config_t* config, uint32_t offset);
```

- `max_concurrent` — quota on requests awaiting their final response, checked on
  both `uvrpc_client_call()` and `uvrpc_client_call_batch()`; exceeding it returns
  `UVRPC_ERROR_RATE_LIMITED`. Oneway sends are not counted (they take no slot).
  Default `UVRPC_MAX_CONCURRENT_REQUESTS` (100).
- `max_pending_callbacks` — size of the callback routing table. Must be a power of
  two in `[64, UVRPC_MAX_PENDING_CALLBACKS]`; anything else silently falls back to
  `UVRPC_DEFAULT_PENDING_CALLBACKS` (`1 << 16`).
- `max_clients` — server-side connection quota. `0` or less selects the 1024
  default; there is no unlimited mode.

::: warning Removed knobs
`uvrpc_config_set_performance_mode`, `uvrpc_config_set_pool_size`,
`uvrpc_config_set_timeout` and `uvrpc_config_set_pump_interval` were deleted: they
were stored and never read. There is no batching mode, no connection pool, and no
per-request timeout in the RPC path (`uvrpc_async_*` helpers take an explicit
timeout argument, which is enforced). Ask for a real timeout on the issue tracker
rather than passing a value that does nothing.
:::

## Server

### `uvrpc_server_t`

Opaque server handle.

```c
typedef struct uvrpc_server uvrpc_server_t;
```

#### `uvrpc_server_create()`

```c
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config);
```

#### `uvrpc_server_register()`

Register a handler for a method name.

```c
int uvrpc_server_register(uvrpc_server_t* server, const char* method,
                          uvrpc_handler_t handler, void* ctx);
```

The handler signature:

```c
typedef void (*uvrpc_handler_t)(uvrpc_request_t* req, void* ctx);
```

#### `uvrpc_server_start()` / `uvrpc_server_stop()`

```c
int uvrpc_server_start(uvrpc_server_t* server);
int uvrpc_server_stop(uvrpc_server_t* server);
```

#### `uvrpc_server_free()`

```c
void uvrpc_server_free(uvrpc_server_t* server);
```

#### Stats & context

```c
int      uvrpc_server_get_client_count(uvrpc_server_t* server);
uint64_t uvrpc_server_get_total_requests(uvrpc_server_t* server);
uint64_t uvrpc_server_get_total_responses(uvrpc_server_t* server);
void     uvrpc_server_set_context(uvrpc_server_t* server, uvrpc_context_t* ctx);
uvrpc_context_t* uvrpc_server_get_context(uvrpc_server_t* server);
```

## Request (server side)

### `uvrpc_request_t`

Passed to a server handler. Fields include `method`, `msgid`, `params`,
`params_size`.

#### `uvrpc_request_send_response()`

Send the final response for a request.

```c
void uvrpc_request_send_response(uvrpc_request_t* req, int status,
                                 const uint8_t* result, size_t result_size);
```

#### `uvrpc_request_send_response_more()`

Send an intermediate streaming chunk (more to follow).

```c
void uvrpc_request_send_response_more(uvrpc_request_t* req,
                                      const uint8_t* result, size_t result_size);
```

#### `uvrpc_request_free()`

```c
void uvrpc_request_free(uvrpc_request_t* req);
```

#### Streaming helpers

```c
int uvrpc_response_send(uvrpc_request_t* req, const uint8_t* result, size_t result_size);
int uvrpc_response_send_stream(uvrpc_request_t* req, const uint8_t* chunk,
                               size_t chunk_size, int is_last);
int uvrpc_response_send_error(uvrpc_request_t* req, int32_t error_code,
                              const char* error_message);
```

## Client

### `uvrpc_client_t`

Opaque client handle.

```c
typedef struct uvrpc_client uvrpc_client_t;
```

#### `uvrpc_client_create()` / `uvrpc_client_free()`

```c
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config);
void uvrpc_client_free(uvrpc_client_t* client);
```

#### `uvrpc_client_connect()` / `uvrpc_client_disconnect()`

```c
int uvrpc_client_connect(uvrpc_client_t* client);
int uvrpc_client_connect_with_callback(uvrpc_client_t* client,
                                       uvrpc_connect_callback_t callback, void* ctx);
void uvrpc_client_disconnect(uvrpc_client_t* client);
```

#### `uvrpc_client_call()`

Normal request/response call. Asynchronous — the callback is invoked when the
response arrives (on the event loop thread).

```c
int uvrpc_client_call(uvrpc_client_t* client, const char* method,
                      const uint8_t* params, size_t params_size,
                      uvrpc_callback_t callback, void* ctx);
```

Callback signature:

```c
typedef void (*uvrpc_callback_t)(uvrpc_response_t* resp, void* ctx);
```

#### `uvrpc_client_call_oneway()`

Fire-and-forget — no response.

```c
int uvrpc_client_call_oneway(uvrpc_client_t* client, const char* method,
                             const uint8_t* params, size_t params_size);
```

#### `uvrpc_client_call_batch()`

Batch multiple calls.

```c
int uvrpc_client_call_batch(uvrpc_client_t* client, const char* method,
                            const uint8_t* params, size_t params_size,
                            uvrpc_callback_t callback, void* ctx);
```

#### Tuning & introspection

```c
int  uvrpc_client_set_max_concurrent(uvrpc_client_t* client, int max);  /* UVRPC_OK / UVRPC_ERROR_INVALID_PARAM */
void uvrpc_client_set_max_retries(uvrpc_client_t* client, int max_retries);
int  uvrpc_client_get_max_retries(uvrpc_client_t* client);
int  uvrpc_client_get_pending_count(uvrpc_client_t* client);
uv_loop_t* uvrpc_client_get_loop(uvrpc_client_t* client);
void uvrpc_client_set_context(uvrpc_client_t* client, uvrpc_context_t* ctx);
uvrpc_context_t* uvrpc_client_get_context(uvrpc_client_t* client);
```

## Response (client side)

### `uvrpc_response_t`

Passed to a client callback. Fields include `status`, `msgid`, `result`,
`result_size`, `error_code`, `error_message`.

```c
void uvrpc_response_free(uvrpc_response_t* resp);
int  uvrpc_response_is_stream_more(uvrpc_response_t* resp);
int  uvrpc_response_is_stream_end(uvrpc_response_t* resp);
```

## Context

### `uvrpc_context_t`

User-data bag attachable to a server or client.

```c
uvrpc_context_t* uvrpc_context_new(void);
uvrpc_context_t* uvrpc_context_new_with_cleanup(void (*cleanup)(void*));
void uvrpc_context_free(uvrpc_context_t* ctx);
void* uvrpc_context_get_data(uvrpc_context_t* ctx);
```

## Allocator

```c
void* uvrpc_alloc(size_t size);
void* uvrpc_calloc(size_t count, size_t size);
void* uvrpc_realloc(void* ptr, size_t size);
void  uvrpc_free(void* ptr);
char* uvrpc_strdup(const char* s);
```

The allocator type is fixed at compile time via `UVRPC_DEFAULT_ALLOCATOR`
(`system`, `mimalloc`, or `custom`). See [Build & Install](/build-install).

## Errors

```c
const char* uvrpc_strerror(int error_code);
```

Error codes (`uvrpc_error_t`): `UVRPC_OK` (0), `UVRPC_ERROR` (-1),
`UVRPC_ERROR_INVALID_PARAM` (-2), `UVRPC_ERROR_NO_MEMORY` (-3),
`UVRPC_ERROR_NOT_CONNECTED` (-4), `UVRPC_ERROR_TIMEOUT` (-5),
`UVRPC_ERROR_TRANSPORT` (-6), `UVRPC_ERROR_TRANSPORT_BUSY` (-7),
`UVRPC_ERROR_CALLBACK_LIMIT` (-8), `UVRPC_ERROR_CANCELLED` (-9),
`UVRPC_ERROR_POOL_EXHAUSTED` (-10), `UVRPC_ERROR_RATE_LIMITED` (-11),
`UVRPC_ERROR_NOT_FOUND` (-12), `UVRPC_ERROR_ALREADY_EXISTS` (-13),
`UVRPC_ERROR_INVALID_STATE` (-14), `UVRPC_ERROR_IO` (-15),
`UVRPC_ERROR_MAX_CLIENTS` (-16).
