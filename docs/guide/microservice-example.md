# Two Services, Two Processes: A Worked Example

UVRPC is a low-level RPC library, not a microservice framework. It does not do
service discovery, load balancing, TLS, or retries across services. Those are
gateway or application concerns. What it does give you is the piece every
service needs: typed calls over a chosen transport, on a single-threaded event
loop you own.

This page walks through a complete two-service system built from that piece:
an inventory service and an audit service, deployed as separate processes on
separate ports, sharing one schema.

| File | Role |
| --- | --- |
| `schema/shop.fbs` | Declares both services in one schema |
| `examples/shop_inventory_server.c` | Owns stock; implements `Stock` / `Reserve` |
| `examples/shop_audit_server.c` | Records events; implements `Record` / `History` |
| `examples/shop_client.c` | Orchestrates both services and asserts the results |

## Build and run

```bash
cmake --build build --target shop_inventory_server shop_audit_server shop_client

./dist/bin/shop_inventory_server tcp://127.0.0.1:7101 &
./dist/bin/shop_audit_server    tcp://127.0.0.1:7102 &
./dist/bin/shop_client
```

`shop_client` prints one line per check and exits non-zero if any fails, so it
doubles as an end-to-end smoke test. The two servers keep running; use `fg` or
kill them when done.

## One schema, two services

Both services live in `schema/shop.fbs` but become independent binaries. The
schema describes the system, not the process that hosts it:

```flatbuffers
namespace shop;

table ReserveRequest  { sku: string; qty: uint32; }
table ReserveResponse { ok: bool; available: uint32; }

rpc_service InventoryService {
    Stock(StockRequest):StockResponse;
    Reserve(ReserveRequest):ReserveResponse;
}

rpc_service ShopAuditService {
    Record(AuditRequest):EmptyResponse (oneway);
    History(HistoryRequest):HistoryResponse;
}
```

`Record` is `(oneway)`: the client arms no callback and the server stub writes
no response. The empty response table is a convention, not a trigger — the
method declaration decides the shape.

## Three things this example gets right

### 1. A business refusal is a successful call

When stock is insufficient, `Reserve` returns `UVRPC_OK` with `ok=false` and
the stock unchanged. Encoding "no stock" as a transport error would make the
caller unable to tell "service unreachable" from "service said no".

```c
shop_ReserveRequest_t req  = { .sku = "widget", .qty = 9 };
shop_ReserveResponse_t resp = {0};
int ret = uvrpc_inventoryservice_Reserve_sync(inventory, &resp, &req, 5000);
check("refused, stock stays 2",
      ret == UVRPC_OK && !resp.ok && resp.available == 2);
```

`StockResponse.found` serves the same purpose: an unknown SKU and a sold-out
one are different answers, so they get different fields.

### 2. Orchestration lives in the caller

A handler runs on its server's event loop, and its request object lives on the
dispatch stack. It cannot pause to await another service, nor send a response
after it returns. So "reserve, then audit" is a caller-side sequence, not a
handler-side one:

```c
/* shop_client.c, abbreviated */
reserve_on_inventory(...);        /* first service */
record_on_audit(actor, action);   /* second service, oneway */
```

If you need a workflow that survives a process restart, that is a job for a
workflow engine outside this library — not for a handler that holds a request
open.

### 3. Query results are copied out

`HistoryResponse.items` is a vector of tables. The generated `_sync()` binding
copies it element by element, strings included, so the answer remains readable
after the response buffer is gone. Free it explicitly when done:

```c
shop_HistoryResponse_t history = {0};
int ret = uvrpc_shopauditservice_History_sync(audit, &history, &req, 5000);
/* ... read history.items[i].actor, .action, .sku, .qty ... */
uvrpc_shopauditservice_History_response_free(&history);
```

`limit=0` means "everything retained"; `limit=1` returns only the newest entry.

## What this example deliberately does not do

- **Service discovery / load balancing.** Addresses are explicit in the client.
  Put a resolver or sidecar in front if instances move.
- **TLS / auth.** Frames are plaintext. Run over a trusted network or behind a
  gateway that terminates TLS. See the project's Non-goals.
- **Cross-service retry or circuit breaking.** `uvrpc_client_set_max_retries()`
  retries synchronous send failures; an unanswered call is a timeout, not a
  trigger for a cross-service policy. Build that in the orchestrator.
- **Multi-threaded scale-out.** Scale by running more single-threaded processes,
  not by adding threads to one loop.

## Verification

`tests/generated_table_array_test.c` covers `_sync` / `_all` / `_any` on the
same schema over SAMELOOP, including the free helpers. The generated bindings
themselves are not committed — `tools/uvrpcc.py` produces them at build time.

See [A Complete Application: KV Service](/guide/kv-example) for the single-service
path, and [Design Philosophy](/guide/design-philosophy) for why the library owns
no threads, locks, or mutable globals.
