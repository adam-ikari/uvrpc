/*
 * Inventory service: the data owner in this two-service example.
 *
 * Run it next to shop_audit_server and talk to both with shop_client; see
 * schema/shop.fbs for the shared contract.
 *
 *   ./dist/bin/shop_inventory_server tcp://127.0.0.1:7101
 *
 * The stock is a fixed table because the example is about the service boundary,
 * not about storage. Reserve decrements in place, so restarting the process
 * resets it to the numbers below.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "shop_inventoryservice_api.h"
#include "shop_reader.h"

typedef struct {
    const char* sku;
    uint32_t available;
} sku_row_t;

static sku_row_t g_stock[] = {
    {"widget", 5},
    {"gizmo", 2},
    {"anvil", 0},
};

#define STOCK_ROWS (sizeof(g_stock) / sizeof(g_stock[0]))

static sku_row_t* find_sku(const char* sku) {
    if (!sku) {
        return NULL;
    }
    for (size_t i = 0; i < STOCK_ROWS; i++) {
        if (strcmp(g_stock[i].sku, sku) == 0) {
            return &g_stock[i];
        }
    }
    return NULL;
}

/* Responses go back as FlatBuffers, not as the client-side struct: the wire
 * has a table root, and the struct is only the generated client's convenience
 * copy of one. */
static void send_stock_response(uvrpc_request_t* req, int found, uint32_t available) {
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    shop_StockResponse_start_as_root(&builder);
    shop_StockResponse_found_add(&builder, found);
    shop_StockResponse_available_add(&builder, available);
    shop_StockResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);                     /* flatcc's allocator, so free() releases it */
    flatcc_builder_clear(&builder);
}

static void send_reserve_response(uvrpc_request_t* req, int ok, uint32_t available) {
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    shop_ReserveResponse_start_as_root(&builder);
    shop_ReserveResponse_ok_add(&builder, ok);
    shop_ReserveResponse_available_add(&builder, available);
    shop_ReserveResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
}

uvrpc_error_t uvrpc_inventoryservice_handle_request(const char* method_name,
                                                    const void* request,
                                                    uvrpc_request_t* req) {
    if (strcmp(method_name, "Stock") == 0) {
        const shop_StockRequest_table_t r = shop_StockRequest_as_root(request);
        const char* sku = shop_StockRequest_sku(r);
        const sku_row_t* row = find_sku(sku);

        printf("shop_inventory_server: Stock(%s) -> %s%u\n",
               sku ? sku : "", row ? "" : "unknown, ", row ? row->available : 0);
        fflush(stdout);

        send_stock_response(req, row != NULL, row ? row->available : 0);
        return UVRPC_OK;

    } else if (strcmp(method_name, "Reserve") == 0) {
        const shop_ReserveRequest_table_t r = shop_ReserveRequest_as_root(request);
        const char* sku = shop_ReserveRequest_sku(r);
        uint32_t qty = shop_ReserveRequest_qty(r);
        sku_row_t* row = find_sku(sku);

        /* Not enough stock is a business answer, not an RPC failure: the call
         * succeeds and says ok=false, with the stock unchanged. Whether the
         * caller treats that as an error is the caller's policy. */
        int ok = row && row->available >= qty;
        if (ok) {
            row->available -= qty;
        }

        printf("shop_inventory_server: Reserve(%s, %u) -> %s, %u left\n",
               sku ? sku : "", qty, ok ? "ok" : "refused",
               row ? row->available : 0);
        fflush(stdout);

        send_reserve_response(req, ok, row ? row->available : 0);
        return UVRPC_OK;
    }

    return UVRPC_ERROR_NOT_FOUND;
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:7101";

    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "shop_inventory_server: cannot create event loop\n");
        return 1;
    }

    /* TCP has no in-process endpoints, so there is no loop registry to pass. */
    uvrpc_server_t* server = uvrpc_inventoryservice_create_server(&loop, address, NULL);
    if (!server) {
        fprintf(stderr, "shop_inventory_server: cannot create server on %s\n", address);
        uv_loop_close(&loop);
        return 1;
    }

    int ret = uvrpc_inventoryservice_start_server(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "shop_inventory_server: start failed: %s\n", uvrpc_strerror(ret));
        uvrpc_inventoryservice_free_server(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("shop_inventory_server: listening on %s\n", address);
    fflush(stdout);

    uv_run(&loop, UV_RUN_DEFAULT);

    uvrpc_inventoryservice_free_server(server);
    uv_run(&loop, UV_RUN_DEFAULT);   /* let close callbacks settle */
    uv_loop_close(&loop);
    return 0;
}
