# A Complete Application: KV Service

The [Quick Start](/quick-start) example writes the wire format by hand. A real
project does not. This page follows the path most projects take: a schema of
your own, bindings generated from it at build time, and a server and a client
that are separate programs.

| File | Role |
| --- | --- |
| `schema/kv.fbs` | Declares `Put` / `Get` / `Delete` on a `KVService` |
| `examples/kv_server.c` | Implements the three handlers |
| `examples/kv_client.c` | Drives the generated API and asserts on the results |
| `tests/generated_api_test.c` | The same bindings under `ctest`, over SAMELOOP |

## Schema

```flatbuffers
namespace kv;

table PutRequest    { key: string; value: string; }
table PutResponse   { ok: bool; }
table GetRequest    { key: string; }
table GetResponse   { found: bool; value: string; }
table DeleteRequest { key: string; }
table DeleteResponse{ deleted: bool; }

rpc_service KVService {
  Put(PutRequest):PutResponse;
  Get(GetRequest):GetResponse;
  Delete(DeleteRequest):DeleteResponse;
}
```

The build runs `tools/uvrpcc.py` on it and compiles the output; nothing is
checked in. `CMakeLists.txt` wires it up with one call:

```cmake
uvrpc_dsl_generate(KV ${SCHEMA_DIR}/kv.fbs ${UVRPC_KV_GEN_DIR} kv_kvservice)
```

That produces `kv_kvservice_api.h`, `_client.c`, `_server_stub.c` and
`_rpc_common.{h,c}` next to flatcc's own `kv_reader.h` / `kv_builder.h`.

## Build and run

```bash
cmake --build build --target kv_server kv_client
./dist/bin/kv_server            # terminal 1, defaults to tcp://127.0.0.1:5555
./dist/bin/kv_client            # terminal 2
```

The client prints one line per check and exits non-zero if any fails:

```
kv_client: talking to tcp://127.0.0.1:5555
  Put(greeting) succeeds                     ok
  Get(greeting) returns hello, kv            ok
  Get(not-there) is found == false           ok
  Delete(greeting) reports deleted           ok
  Delete(greeting) again reports not deleted ok
  Get(greeting) is gone                      ok
  Put(value too long) is INVALID_PARAM       ok
kv_client: 0 failure(s)
```

## What the generated bindings own

Three facts make the difference between code that works and code that corrupts
memory. All three are demonstrated in the example.

**Creating a client does not wait for the connection.** `create_client()` starts
it and returns. The result arrives in the connect callback; a sync call made
before that returns `UVRPC_ERROR_NOT_CONNECTED`. The client pumps the loop with
`uv_run(UV_RUN_NOWAIT)` until the callback fires, with a deadline.

**A handler refusal must be sent as an error frame.** Inside the stub, a handler
returns a non-`UVRPC_OK` error and the stub calls
`uvrpc_response_send_error()`. Reporting it through
`uvrpc_request_send_response()` would produce a *success* frame carrying an empty
payload.

**Responses handed back by `_sync()` own their strings.** The response buffer is
freed before `_sync()` returns, so every string field is a copy. Release them
with the generated helper:

```c
kv_GetResponse_t response = {0};
uvrpc_error_t rc = uvrpc_kvservice_Get_sync(client, &response, &request, 5000);
if (rc == UVRPC_OK && response.found) {
    printf("%s\n", response.value);       /* valid until the free below */
}
uvrpc_kvservice_Get_response_free(&response);
```

Raw callbacks have the opposite contract: the payload is valid only for the
duration of the callback, so anything you keep must be copied there.

The combinator helpers follow the same rule. `uvrpc_<svc>_<Method>_all()` returns
a heap array of structs that own copies; release it with
`uvrpc_<svc>_<Method>_results_free(results, count)`, which frees each element and
the array. `uvrpc_<svc>_<Method>_any()` writes into a struct you supply; release
its contents with `..._result_free()` and leave the struct itself alone. Both
write only on `UVRPC_OK`, so zero-initialise before the call.

## The same bindings under test

`tests/generated_api_test.c` runs the generated code over SAMELOOP, a transport
that connects endpoints through a registry inside one process. The sync wrapper
runs the loop inline, which also serves the server's end of each call, so the
test needs no port and no second executable.

```bash
ctest --test-dir build -R generated_api -V
```

It asserts what the examples demonstrate — the sync path completes, the returned
string is readable after `_sync()` returns, the free helper clears it, a missing
key reports `found == false`, a refused `Put` arrives as `UVRPC_ERROR_INVALID_PARAM`,
and both combinator helpers hand back copies that can be freed.
