/*
 * UVRPC DSL Example - Using Generated RPC Code
 *
 * Shows the two halves of the generated API against the schema's own service:
 * the server side owns routing (uvrpc_mathservice_create_server), the client
 * side gets a typed call per method (uvrpc_mathservice_Add).
 *
 * Run as two processes:
 *   ./rpc_dsl_usage_example server
 *   ./rpc_dsl_usage_example client
 */

#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/uvrpc.h"
#include "rpc_mathservice_api.h"
#include "rpc_api_builder.h"

/* The generated server stub dispatches by method name and calls into this. The
 * generator does not implement the logic -- that is the part a schema cannot
 * know -- so a demo using the generated server has to provide it. */
int uvrpc_mathservice_handle_request(const char* method_name,
                                     const void* request,
                                     uvrpc_request_t* req) {
    int32_t result = 0;

    if (strcmp(method_name, "Add") == 0) {
        rpc_MathAddRequest_table_t r = rpc_MathAddRequest_as_root(request);
        result = rpc_MathAddRequest_a(r) + rpc_MathAddRequest_b(r);
    } else if (strcmp(method_name, "Subtract") == 0) {
        rpc_MathSubtractRequest_table_t r = rpc_MathSubtractRequest_as_root(request);
        result = rpc_MathSubtractRequest_a(r) - rpc_MathSubtractRequest_b(r);
    } else if (strcmp(method_name, "Multiply") == 0) {
        rpc_MathMultiplyRequest_table_t r = rpc_MathMultiplyRequest_as_root(request);
        result = rpc_MathMultiplyRequest_a(r) * rpc_MathMultiplyRequest_b(r);
    } else if (strcmp(method_name, "Divide") == 0) {
        rpc_MathDivideRequest_table_t r = rpc_MathDivideRequest_as_root(request);
        int32_t b = rpc_MathDivideRequest_b(r);
        result = b != 0 ? rpc_MathDivideRequest_a(r) / b : 0;
    } else {
        uvrpc_response_send_error(req, UVRPC_ERROR_INVALID_PARAM, "unknown method");
        return UVRPC_ERROR_INVALID_PARAM;
    }

    flatcc_builder_t builder;
    flatcc_builder_init(&builder);
    rpc_MathAddResponse_start_as_root(&builder);
    rpc_MathAddResponse_result_add(&builder, result);
    rpc_MathAddResponse_end_as_root(&builder);

    size_t size = 0;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);
    uvrpc_request_send_response(req, UVRPC_OK, buf, size);

    flatcc_builder_aligned_free(buf);
    flatcc_builder_clear(&builder);
    return UVRPC_OK;
}

/* Response callback */
static void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        rpc_MathAddResponse_table_t result = rpc_MathAddResponse_as_root(resp->result);
        printf("[Client] Add result: %lld\n", (long long)rpc_MathAddResponse_result(result));
    } else {
        fprintf(stderr, "[Client] Error: %d\n", resp->status);
    }
    uvrpc_response_free(resp);
    (void)ctx;
}

int main(int argc, char** argv) {
    const char* mode = (argc > 1) ? argv[1] : "server";
    const char* address = (argc > 2) ? argv[2] : "tcp://127.0.0.1:5555";

    uv_loop_t loop;
    uv_loop_init(&loop);

    if (strcmp(mode, "server") == 0) {
        /* Server mode. create_server wires up routing for every method in the
         * service; there is nothing to register by hand. */
        printf("=== UVRPC DSL Server ===\n");
        printf("Address: %s\n\n", address);

        uvrpc_server_t* server = uvrpc_mathservice_create_server(&loop, address, NULL);
        if (!server) {
            fprintf(stderr, "Failed to create server\n");
            return 1;
        }
        if (uvrpc_mathservice_start_server(server) != UVRPC_OK) {
            fprintf(stderr, "Failed to start server\n");
            return 1;
        }

        printf("MathService running. Press Ctrl+C to stop.\n\n");
        uv_run(&loop, UV_RUN_DEFAULT);

        uvrpc_mathservice_free_server(server);
    } else if (strcmp(mode, "client") == 0) {
        /* Client mode */
        printf("=== UVRPC DSL Client ===\n");
        printf("Address: %s\n\n", address);

        uvrpc_client_t* client = uvrpc_mathservice_create_client(&loop, address, NULL, NULL, NULL);
        if (!client) {
            fprintf(stderr, "Failed to create client\n");
            return 1;
        }
        if (uvrpc_client_connect(client) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect\n");
            return 1;
        }

        /* Pump the loop until the connect completes. */
        for (int i = 0; i < 50; i++) {
            uv_run(&loop, UV_RUN_DEFAULT);
        }
        printf("Connected!\n\n");

        /* A typed call: build the request, hand it to the generated wrapper. */
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);

        rpc_MathAddRequest_start_as_root(&builder);
        rpc_MathAddRequest_a_add(&builder, 10);
        rpc_MathAddRequest_b_add(&builder, 20);
        rpc_MathAddRequest_end_as_root(&builder);

        size_t size = 0;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        rpc_MathAddRequest_table_t request = rpc_MathAddRequest_as_root(buf);

        printf("Calling Add(10, 20)...\n");
        uvrpc_mathservice_Add(client, on_response, NULL, request);

        /* Let the call and its response complete. */
        for (int i = 0; i < 50; i++) {
            uv_run(&loop, UV_RUN_DEFAULT);
        }

        flatcc_builder_aligned_free(buf);
        flatcc_builder_clear(&builder);
        uvrpc_mathservice_free_client(client);
    } else {
        fprintf(stderr, "usage: %s <server|client> [address]\n", argv[0]);
        return 1;
    }

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
    return 0;
}