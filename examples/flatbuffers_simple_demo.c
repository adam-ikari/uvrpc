/**
 * UVRPC FlatBuffers Simple Demo
 * Demonstrates using FlatBuffers for type-safe request encoding
 */

#include "../include/uvrpc.h"
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
    printf("[CLIENT] Response: status=%d, msgid=%u, result_size=%zu\n",
           resp->status, resp->msgid, resp->result_size);

    if (resp->status == UVRPC_OK && resp->result && resp->result_size >= 4) {
        int32_t result = *(int32_t*)resp->result;
        printf("[CLIENT] Result: %d\n", result);
    }
    g_running = 0;
}

int main(int argc, char** argv) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";

    printf("=== UVRPC FlatBuffers Simple Demo ===\n");
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

    /* Prepare request data (add 10 + 20) */
    int32_t params[2] = {10, 20};
    printf("[CLIENT] Calling 'add' with params %d + %d\n", params[0], params[1]);
    uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, NULL);

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