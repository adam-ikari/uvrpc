/*
 * Client for the two-service example: it is the orchestrator.
 *
 *   ./dist/bin/shop_inventory_server tcp://127.0.0.1:7101 &
 *   ./dist/bin/shop_audit_server    tcp://127.0.0.1:7102 &
 *   ./dist/bin/shop_client
 *
 * Composition lives here, not in a server handler, and that is a constraint
 * rather than a style choice: a handler runs on the server's own loop and its
 * request object lives on the dispatch stack, so it cannot park, wait for a
 * second service, and answer afterwards. A caller (or a gateway process built
 * the same way) can interleave as many services as it likes.
 *
 * Exit code is 0 only if every check passes, so this doubles as a smoke test.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "shop_inventoryservice_api.h"
#include "shop_shopauditservice_api.h"

#define CALL_TIMEOUT_MS 2000

static int failures;
static int inventory_connected = -1;   /* -1: still in flight */
static int audit_connected = -1;

static void on_inventory_connect(int status, void* ctx) {
    (void)ctx;
    inventory_connected = status;
}

static void on_audit_connect(int status, void* ctx) {
    (void)ctx;
    audit_connected = status;
}

static void check(const char* what, int ok) {
    printf("  %-46s %s\n", what, ok ? "ok" : "FAILED");
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

/* create_client() only starts the connect; the answer arrives in a callback.
 * Pump the loop until both are done or the deadline passes. */
static int wait_for_connections(uv_loop_t* loop) {
    uint64_t deadline = uv_hrtime() + 5ULL * 1000 * 1000 * 1000;
    while ((inventory_connected < 0 || audit_connected < 0) &&
           uv_hrtime() < deadline) {
        uv_run(loop, UV_RUN_NOWAIT);
        uv_sleep(1);
    }
    return inventory_connected == 0 && audit_connected == 0;
}

/* One order line: reserve stock, then tell the audit service about it. Both
 * calls are made by this process, one after the other; neither server knows
 * the other exists. */
static void reserve_and_record(uvrpc_client_t* inventory,
                               uvrpc_client_t* audit,
                               const char* sku, uint32_t qty) {
    shop_ReserveRequest_t reserve = { .sku = sku, .qty = qty };
    shop_ReserveResponse_t reserved = {0};
    int ret = uvrpc_inventoryservice_Reserve_sync(inventory, &reserved, &reserve,
                                                  CALL_TIMEOUT_MS);

    char what[96];
    snprintf(what, sizeof(what), "Reserve(%s, %u)", sku, qty);
    check(what, ret == UVRPC_OK && reserved.ok);

    if (ret != UVRPC_OK || !reserved.ok) {
        return;
    }

    /* Oneway: no callback, no response. The call returns once the frame is
     * handed to the transport. */
    shop_AuditRequest_t entry = {
        .actor = "shop_client",
        .action = "reserve",
        .sku = sku,
        .qty = qty,
    };
    ret = uvrpc_shopauditservice_Record(audit, &entry);
    snprintf(what, sizeof(what), "Record(%s, %u) queued", sku, qty);
    check(what, ret == UVRPC_OK);
}

int main(int argc, char** argv) {
    const char* inventory_address = argc > 1 ? argv[1] : "tcp://127.0.0.1:7101";
    const char* audit_address = argc > 2 ? argv[2] : "tcp://127.0.0.1:7102";

    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "shop_client: cannot create event loop\n");
        return 1;
    }

    /* NULL registry: TCP has no in-process endpoints to register. */
    uvrpc_client_t* inventory = uvrpc_inventoryservice_create_client(
        &loop, inventory_address, NULL, on_inventory_connect, NULL);
    uvrpc_client_t* audit = uvrpc_shopauditservice_create_client(
        &loop, audit_address, NULL, on_audit_connect, NULL);
    if (!inventory || !audit) {
        fprintf(stderr, "shop_client: cannot create clients\n");
        return 1;
    }

    if (!wait_for_connections(&loop)) {
        fprintf(stderr, "shop_client: connect failed (inventory=%d audit=%d)\n",
                inventory_connected, audit_connected);
        uvrpc_inventoryservice_free_client(inventory);
        uvrpc_shopauditservice_free_client(audit);
        uv_loop_close(&loop);
        return 1;
    }

    printf("shop_client: inventory=%s audit=%s\n",
           inventory_address, audit_address);

    /* 1. The data owner answers a read. */
    {
        shop_StockRequest_t req = { .sku = "widget" };
        shop_StockResponse_t resp = {0};
        int ret = uvrpc_inventoryservice_Stock_sync(inventory, &resp, &req,
                                                    CALL_TIMEOUT_MS);
        check("Stock(widget) is 5", ret == UVRPC_OK && resp.found &&
                                     resp.available == 5);
    }

    /* 2. An unknown SKU is a successful call that says found=false. */
    {
        shop_StockRequest_t req = { .sku = "no-such-sku" };
        shop_StockResponse_t resp = { .found = true };
        int ret = uvrpc_inventoryservice_Stock_sync(inventory, &resp, &req,
                                                    CALL_TIMEOUT_MS);
        check("Stock(no-such-sku) is found=false",
              ret == UVRPC_OK && !resp.found);
    }

    /* 3. Reserve succeeds and decrements. */
    {
        shop_ReserveRequest_t req = { .sku = "widget", .qty = 3 };
        shop_ReserveResponse_t resp = {0};
        int ret = uvrpc_inventoryservice_Reserve_sync(inventory, &resp, &req,
                                                      CALL_TIMEOUT_MS);
        check("Reserve(widget, 3) leaves 2",
              ret == UVRPC_OK && resp.ok && resp.available == 2);
    }

    /* 4. Not enough stock: still UVRPC_OK, ok=false, stock unchanged. */
    {
        shop_ReserveRequest_t req = { .sku = "widget", .qty = 9 };
        shop_ReserveResponse_t resp = {0};
        int ret = uvrpc_inventoryservice_Reserve_sync(inventory, &resp, &req,
                                                      CALL_TIMEOUT_MS);
        check("Reserve(widget, 9) refused, stock stays 2",
              ret == UVRPC_OK && !resp.ok && resp.available == 2);
    }

    /* 5. A sold-out SKU exists but has nothing left. */
    {
        shop_StockRequest_t req = { .sku = "anvil" };
        shop_StockResponse_t resp = {0};
        int ret = uvrpc_inventoryservice_Stock_sync(inventory, &resp, &req,
                                                    CALL_TIMEOUT_MS);
        check("Stock(anvil) is found=true, 0 available",
              ret == UVRPC_OK && resp.found && resp.available == 0);
    }

    /* 6. Two order lines, each reserving and auditing. */
    reserve_and_record(inventory, audit, "gizmo", 1);
    reserve_and_record(inventory, audit, "widget", 1);

    /* 7. The audit side answers with a vector of tables. The sync wrapper
     *    copied each element and its strings, so `history` is safe to read
     *    after the call returns -- and must be freed by the generated
     *    response_free(). */
    {
        shop_HistoryRequest_t req = { .limit = 0 };   /* everything retained */
        shop_HistoryResponse_t history = {0};
        int ret = uvrpc_shopauditservice_History_sync(audit, &history, &req,
                                                      CALL_TIMEOUT_MS);
        check("History returns two entries",
              ret == UVRPC_OK && history.items_size == 2);
        check("newest entry is the widget reservation",
              history.items_size == 2 &&
              history.items[1].sku && strcmp(history.items[1].sku, "widget") == 0 &&
              history.items[1].qty == 1 &&
              history.items[1].actor && strcmp(history.items[1].actor, "shop_client") == 0);

        uvrpc_shopauditservice_History_response_free(&history);
        check("response_free cleared the vector",
              history.items == NULL && history.items_size == 0);
    }

    /* 8. limit is honoured, and it selects the most recent entries. */
    {
        shop_HistoryRequest_t req = { .limit = 1 };
        shop_HistoryResponse_t history = {0};
        int ret = uvrpc_shopauditservice_History_sync(audit, &history, &req,
                                                      CALL_TIMEOUT_MS);
        check("History(limit=1) returns the newest entry",
              ret == UVRPC_OK && history.items_size == 1 &&
              history.items[0].sku && strcmp(history.items[0].sku, "widget") == 0);
        uvrpc_shopauditservice_History_response_free(&history);
    }

    uvrpc_inventoryservice_free_client(inventory);
    uvrpc_shopauditservice_free_client(audit);
    uv_run(&loop, UV_RUN_DEFAULT);   /* let close callbacks settle */
    uv_loop_close(&loop);

    printf("shop_client: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
