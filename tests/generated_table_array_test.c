/*
 * Vectors of tables, end to end, over SAMELOOP.
 *
 * A `[Table]` field is copied element by element: each element is a FlatBuffers
 * table inside the response buffer, and every string in it points into that
 * buffer, which is freed before the wrapper returns. The generator used to emit
 * this path without ever compiling it -- the element struct was missing, the
 * vector was read through the wrong typedef, and the element accessor was
 * handed `&item` instead of `item` -- so this test is what keeps the four
 * places that build or free the vector honest:
 *
 *   - rpc_common.h: the element struct exists and precedes the message struct;
 *   - client.c: _sync() copies the vector and its strings;
 *   - rpc_common.c: _all() / _any() copy it too;
 *   - response_free / result_free release every string, then the array.
 */

#include <stdio.h>
#include <string.h>

#include <uv.h>
#include "ta_tableservice_api.h"
#include "uvrpc_async.h"
#include "table_arrays_reader.h"

#define CALL_TIMEOUT_MS 5000

static int failures;

static void check(const char* what, int ok) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        failures++;
    }
}

/* Echo the vector back, rebuilding each element from the request. */
uvrpc_error_t uvrpc_tableservice_handle_request(const char* method_name,
                                                const void* request,
                                                uvrpc_request_t* req) {
    if (strcmp(method_name, "Echo") != 0) {
        return UVRPC_ERROR_NOT_FOUND;
    }

    ta_EchoRequest_table_t echo = ta_EchoRequest_as_root(request);

    ta_Item_vec_t items = ta_EchoRequest_items(echo);
    size_t count = flatbuffers_vec_len(items);

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    ta_EchoResponse_start_as_root(&builder);
    if (count > 0) {
        ta_EchoResponse_items_start(&builder);
        for (size_t i = 0; i < count; i++) {
            ta_Item_table_t item = ta_Item_vec_at(items, i);
            const char* name = ta_Item_name(item);
            flatbuffers_string_ref_t name_ref = flatbuffers_string_create(
                &builder, name ? name : "", name ? strlen(name) : 0);
            ta_EchoResponse_items_push_create(&builder, name_ref, ta_Item_qty(item));
        }
        ta_EchoResponse_items_end(&builder);
    }
    ta_EchoResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    free(buf);
    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

static ta_Item_t make_item(const char* name, uint32_t qty) {
    ta_Item_t item = { .name = name, .qty = qty };
    return item;
}

int main(void) {
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: cannot create event loop\n");
        return 1;
    }

    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();
    if (!registry) {
        printf("FAIL: cannot create loop registry\n");
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_server_t* server = uvrpc_tableservice_create_server(
        &loop, "sameloop://generated_table_array", registry);
    if (!server || uvrpc_tableservice_start_server(server) != UVRPC_OK) {
        printf("FAIL: could not start server\n");
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_client_t* client = uvrpc_tableservice_create_client(
        &loop, "sameloop://generated_table_array", registry, NULL, NULL);
    if (!client) {
        printf("FAIL: could not create client\n");
        uvrpc_tableservice_stop_server(server);
        uvrpc_tableservice_free_server(server);
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    /* --- two elements, strings and scalars --------------------------- */
    {
        ta_Item_t items[2] = { make_item("bolt", 12), make_item("nut", 7) };
        ta_EchoRequest_t request = { .items = items, .items_size = 2 };
        ta_EchoResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_tableservice_Echo_sync(client, &response,
                                                         &request, CALL_TIMEOUT_MS);
        check("sync Echo returns UVRPC_OK", ret == UVRPC_OK);
        check("response vector has two elements", response.items_size == 2);
        /* Read after _sync() returned: these are copies. */
        check("element 0 name survives the call",
              response.items && response.items[0].name &&
              strcmp(response.items[0].name, "bolt") == 0);
        check("element 0 qty survives the call",
              response.items && response.items[0].qty == 12);
        check("element 1 name survives the call",
              response.items && response.items[1].name &&
              strcmp(response.items[1].name, "nut") == 0);

        uvrpc_tableservice_Echo_response_free(&response);
        check("response_free cleared the vector",
              response.items == NULL && response.items_size == 0);
        /* Freeing twice is harmless: the generated free checks for NULL. */
        uvrpc_tableservice_Echo_response_free(&response);
    }

    /* --- an empty vector -------------------------------------------- */
    {
        ta_EchoRequest_t request = {0};
        ta_EchoResponse_t response = {0};

        uvrpc_error_t ret = uvrpc_tableservice_Echo_sync(client, &response,
                                                         &request, CALL_TIMEOUT_MS);
        check("empty request returns UVRPC_OK", ret == UVRPC_OK);
        check("empty vector arrives as NULL with size 0",
              response.items == NULL && response.items_size == 0);
        uvrpc_tableservice_Echo_response_free(&response);
    }

    /* --- _all: one vector per call, results must not be shared ------- */
    {
        uvrpc_async_ctx_t* actx = uvrpc_async_ctx_new(&loop);
        if (!actx) {
            printf("FAIL: cannot create async context\n");
        } else {
            ta_Item_t left[1] = { make_item("left", 1) };
            ta_Item_t right[2] = { make_item("right-a", 2), make_item("right-b", 3) };
            ta_EchoRequest_t requests[2] = {
                { .items = left, .items_size = 1 },
                { .items = right, .items_size = 2 },
            };
            uvrpc_client_t* clients[2] = { client, client };
            ta_EchoResponse_t* all = NULL;

            uvrpc_error_t ret = uvrpc_tableservice_echo_all(
                actx, clients, requests, &all, 2, CALL_TIMEOUT_MS);
            check("echo_all returns UVRPC_OK", ret == UVRPC_OK);
            check("echo_all copied the first vector",
                  all && all[0].items_size == 1 && all[0].items[0].name &&
                  strcmp(all[0].items[0].name, "left") == 0);
            check("echo_all copied the second vector",
                  all && all[1].items_size == 2 && all[1].items[1].name &&
                  strcmp(all[1].items[1].name, "right-b") == 0 &&
                  all[1].items[1].qty == 3);
            uvrpc_tableservice_echo_results_free(all, 2);

            ta_EchoResponse_t single = {0};
            int index = -1;
            ret = uvrpc_tableservice_echo_any(actx, clients, requests, &single,
                                              &index, 2, CALL_TIMEOUT_MS);
            check("echo_any returns UVRPC_OK with a valid index",
                  ret == UVRPC_OK && index >= 0 && index < 2);
            check("echo_any copied the winning vector",
                  single.items_size == (size_t)(index == 0 ? 1 : 2) &&
                  single.items && single.items[0].name != NULL);
            uvrpc_tableservice_echo_result_free(&single);

            uvrpc_async_ctx_free(actx);
        }
    }

    /* Same teardown order as generated_strings_test: the client closes first
     * and its close callbacks run before the server is torn down. Reversing
     * this leaves loop handles alive, and valgrind then reports the loop's own
     * allocations as lost. */
    uvrpc_tableservice_free_client(client);
    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_tableservice_stop_server(server);
    uvrpc_tableservice_free_server(server);
    uvbus_loop_registry_free(registry);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    printf("generated_table_array_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
