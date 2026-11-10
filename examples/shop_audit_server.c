/*
 * Audit service: the second deployable in this example.
 *
 *   ./dist/bin/shop_audit_server tcp://127.0.0.1:7102
 *
 * Record() is oneway -- the generator treats a method whose response is
 * EmptyResponse as fire-and-forget, so the client does not wait and the server
 * sends nothing back. History() answers with a vector of tables, which is the
 * shape a query endpoint usually needs: the generated client copies it element
 * by element, strings included, so the answer outlives the response buffer.
 *
 * The log keeps the last AUDIT_SLOTS entries in a ring. Older ones are dropped
 * silently, which is what an audit trail with bounded memory does.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "shop_shopauditservice_api.h"
#include "shop_reader.h"

#define AUDIT_SLOTS 32
#define AUDIT_FIELD_MAX 63

typedef struct {
    char actor[AUDIT_FIELD_MAX + 1];
    char action[AUDIT_FIELD_MAX + 1];
    char sku[AUDIT_FIELD_MAX + 1];
    uint32_t qty;
} audit_slot_t;

static audit_slot_t g_entries[AUDIT_SLOTS];
static size_t g_recorded;   /* total recorded, including the ones aged out */

static void copy_field(char* dst, const char* src) {
    /* FlatBuffers strings are not NUL-terminated in the buffer, so they go
     * through the length or into a copy -- never straight into strcpy(). */
    snprintf(dst, AUDIT_FIELD_MAX + 1, "%s", src ? src : "");
}

static void send_history(uvrpc_request_t* req, uint32_t limit) {
    size_t retained = g_recorded < AUDIT_SLOTS ? g_recorded : AUDIT_SLOTS;
    size_t count = retained;
    if (limit > 0 && limit < count) {
        count = limit;
    }
    /* Absolute index of the oldest entry this answer includes: the ring drops
     * the oldest first, so a limit trims from that end and keeps the newest. */
    size_t first = g_recorded - count;

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    shop_HistoryResponse_start_as_root(&builder);
    if (count > 0) {
        /* A table vector field gets its offset only when the field scope is
         * open: items_start, push the elements, items_end. */
        shop_HistoryResponse_items_start(&builder);
        for (size_t i = 0; i < count; i++) {
            const audit_slot_t* e = &g_entries[(first + i) % AUDIT_SLOTS];

            /* A table's create cannot omit a string field, so each one is
             * written into the buffer first and its ref passed along. */
            flatbuffers_string_ref_t actor = flatbuffers_string_create(
                &builder, e->actor, strlen(e->actor));
            flatbuffers_string_ref_t action = flatbuffers_string_create(
                &builder, e->action, strlen(e->action));
            flatbuffers_string_ref_t sku = flatbuffers_string_create(
                &builder, e->sku, strlen(e->sku));

            shop_HistoryResponse_items_push_create(&builder, actor, action, sku, e->qty);
        }
        shop_HistoryResponse_items_end(&builder);
    }
    shop_HistoryResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
}

uvrpc_error_t uvrpc_shopauditservice_handle_request(const char* method_name,
                                                    const void* request,
                                                    uvrpc_request_t* req) {
    if (strcmp(method_name, "Record") == 0) {
        const shop_AuditRequest_table_t r = shop_AuditRequest_as_root(request);
        audit_slot_t* slot = &g_entries[g_recorded % AUDIT_SLOTS];

        copy_field(slot->actor, shop_AuditRequest_actor(r));
        copy_field(slot->action, shop_AuditRequest_action(r));
        copy_field(slot->sku, shop_AuditRequest_sku(r));
        slot->qty = shop_AuditRequest_qty(r);
        g_recorded++;

        /* Oneway: nothing is sent back. A reply here would have no callback
         * slot on the client to land in. */
        return UVRPC_OK;

    } else if (strcmp(method_name, "History") == 0) {
        const shop_HistoryRequest_table_t r = shop_HistoryRequest_as_root(request);
        uint32_t limit = shop_HistoryRequest_limit(r);

        printf("shop_audit_server: History(limit=%u) -> %zu retained\n",
               limit, g_recorded < AUDIT_SLOTS ? g_recorded : AUDIT_SLOTS);
        fflush(stdout);

        send_history(req, limit);
        return UVRPC_OK;
    }

    return UVRPC_ERROR_NOT_FOUND;
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:7102";

    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "shop_audit_server: cannot create event loop\n");
        return 1;
    }

    uvrpc_server_t* server = uvrpc_shopauditservice_create_server(&loop, address, NULL);
    if (!server) {
        fprintf(stderr, "shop_audit_server: cannot create server on %s\n", address);
        uv_loop_close(&loop);
        return 1;
    }

    int ret = uvrpc_shopauditservice_start_server(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "shop_audit_server: start failed: %s\n", uvrpc_strerror(ret));
        uvrpc_shopauditservice_free_server(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("shop_audit_server: listening on %s\n", address);
    fflush(stdout);

    uv_run(&loop, UV_RUN_DEFAULT);

    uvrpc_shopauditservice_free_server(server);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);
    return 0;
}
