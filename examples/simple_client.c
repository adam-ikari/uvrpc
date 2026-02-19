/**
 * UVRPC Simple Client Example
 * Demonstrates basic RPC client usage with proper error handling
 * Uses context structure instead of global variables
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Client context - stores all state locally */
typedef struct {
    volatile int running;
    volatile int connected;
    volatile int received;
    uv_loop_t* loop;
} client_context_t;

/* Connection callback */
void on_connect(int status, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    if (status == 0) {
        context->connected = 1;
        printf("[CLIENT] Connected successfully\n");
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
        context->running = 0;
    }
}

/* Response callback */
void on_response(uvrpc_response_t* resp, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    context->received = 1;
    
    if (resp->status == UVRPC_OK && resp->result_size == 4) {
        int32_t result = *(int32_t*)resp->result;
        printf("[CLIENT] Result: %d + %d = %d\n", 10, 20, result);
    } else if (resp->status == UVRPC_ERROR_CALLBACK_LIMIT) {
        printf("[CLIENT] Pending buffer full - retry later\n");
    } else {
        printf("[CLIENT] Request failed: %d\n", resp->status);
    }
    
    context->running = 0;  /* Exit after receiving response */
}

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "127.0.0.1:5555";
    
    printf("[CLIENT] Starting client, connecting to: %s\n", address);
    
    /* Create loop */
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "[CLIENT] Failed to init loop\n");
        return 1;
    }
    
    /* Create context - stores all state locally */
    client_context_t context = {
        .running = 1,
        .connected = 0,
        .received = 0,
        .loop = &loop
    };
    
    /* Create config with pending buffer size */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    uvrpc_config_set_max_pending_callbacks(config, 64);  /* Set pending buffer size */
    
    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "[CLIENT] Failed to create client\n");
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Connect with callback - pass context as user data */
    int ret = uvrpc_client_connect_with_callback(client, on_connect, &context);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to initiate connect: %d\n", ret);
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Wait for connection */
    int iterations = 0;
    while (!context.connected && context.running && iterations < 100) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }
    
    if (!context.connected) {
        fprintf(stderr, "[CLIENT] Connection timeout\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }
    
    /* Call RPC */
    int32_t params[2] = {10, 20};
    printf("[CLIENT] Calling 'add' with params %d + %d\n", params[0], params[1]);
    
    ret = uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, &context);
    
    if (ret == UVRPC_OK) {
        printf("[CLIENT] Request sent successfully\n");
    } else if (ret == UVRPC_ERROR_CALLBACK_LIMIT) {
        printf("[CLIENT] Pending buffer full - cannot send request\n");
        context.running = 0;
    } else {
        fprintf(stderr, "[CLIENT] Failed to call: %d\n", ret);
        context.running = 0;
    }
    
    /* Wait for response */
    iterations = 0;
    while (context.running && iterations < 50) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }
    
    if (!context.received) {
        printf("[CLIENT] No response received\n");
    }
    
    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[CLIENT] Exiting\n");
    return 0;
}