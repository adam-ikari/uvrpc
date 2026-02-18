/**
 * UVRPC FlatBuffers Demo
 * Demonstrates using FlatBuffers generated code for type-safe RPC
 */

#include "../include/uvrpc.h"
#include "../generated/rpc_example_builder.h"
#include "../generated/rpc_example_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_running = 1;
static volatile int g_connected = 0;

/* Connection callback */
void on_connect(int status, void* ctx) {
    (void)ctx;
    printf("[CLIENT] Connected: %s\n", status == 0 ? "OK" : "FAILED");
    if (status == 0) {
        g_connected = 1;
    } else {
        g_running = 0;
    }
}

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    if (resp->status == UVRPC_OK && resp->result && resp->result_size > 0) {
        example_AddResponse_table_t response = example_AddResponse_as_root(resp->result);
        if (response) {
            printf("[CLIENT] Result: %d\n", example_AddResponse_result(response));
        }
    }
    g_running = 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";

    printf("=== UVRPC FlatBuffers Demo ===\n");
    printf("Address: %s\n\n", address);

    /* Create client */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    /* Transport type auto-detected from address prefix */

    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    /* Connect */
    uvrpc_client_connect_with_callback(client, on_connect, NULL);

    /* Wait for connection */
    int iterations = 0;
    while (!g_connected && g_running && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }

    if (!g_connected) {
        fprintf(stderr, "Connection timeout\n");
        uvrpc_client_free(client);
        return 1;
    }

    /* Encode request using FlatBuffers */
    flatcc_builder_t builder;
    flatcc_builder_init(&builder);

    example_AddRequest_start_as_root(&builder);
    example_AddRequest_a_add(&builder, 10);
    example_AddRequest_b_add(&builder, 20);
    example_AddRequest_end_as_root(&builder);

    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&builder, &size);

    printf("[CLIENT] Calling 'add(10, 20)' using FlatBuffers\n");
    uvrpc_client_call(client, "add", buf, size, on_response, NULL);

    /* Free the buffer allocated by flatcc_builder_finalize_buffer */
    free(buf);
    flatcc_builder_clear(&builder);

    /* Wait for response */
    iterations = 0;
    while (g_running && iterations < 50) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }

    /* Cleanup */
    uvrpc_client_free(client);
    uv_loop_close(&loop);

    printf("=== Demo Complete ===\n");
    return 0;
}