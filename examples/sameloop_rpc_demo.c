/**
 * UVRPC SAMELOOP RPC Demo
 *
 * This example demonstrates how to use SAMELOOP transport for
 * ultra-fast in-process communication within the same event loop.
 *
 * SAMELOOP is ideal for:
 * - High-performance in-process module communication
 * - Zero-copy data transfer
 * - Ultra-low latency RPC (faster than INPROC)
 *
 * Key features:
 * - Zero locks: no synchronization overhead
 * - Zero copies: direct pointer passing
 * - Stack overflow protection: message queuing
 *
 * NOTE: SAMELOOP only works when server and client use the same
 * uv_loop_t instance, not just the same thread.
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Global flag for graceful shutdown */
static volatile int g_running = 1;

/* Signal handler */
void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

/* Server context */
typedef struct {
    int request_count;
} server_context_t;

/* Client context */
typedef struct {
    int connected;
    int received;
} client_context_t;

/* Server request handlers */
void add_handler(uvrpc_request_t* req, void* ctx) {
    server_context_t* server_ctx = (server_context_t*)ctx;
    server_ctx->request_count++;

    printf("[SERVER] Received request: %s\n", req->method);

    if (strcmp(req->method, "add") == 0 && req->params_size == 8) {
        int32_t* params = (int32_t*)req->params;
        int32_t result = params[0] + params[1];

        printf("[SERVER] Computing: %d + %d = %d\n", params[0], params[1], result);

        uvrpc_response_send(req, (uint8_t*)&result, sizeof(result));
    } else {
        /* Invalid request - send empty response */
        uvrpc_response_send(req, NULL, 0);
    }
}

void echo_handler(uvrpc_request_t* req, void* ctx) {
    server_context_t* server_ctx = (server_context_t*)ctx;
    server_ctx->request_count++;

    printf("[SERVER] Echoing: %s\n", req->method);

    /* Echo back the params as result */
    uvrpc_response_send(req, req->params, req->params_size);
}

/* Client callbacks */
void on_connect(int status, void* ctx) {
    client_context_t* client_ctx = (client_context_t*)ctx;

    if (status == 0) {
        client_ctx->connected = 1;
        printf("[CLIENT] Connected successfully\n");
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
        g_running = 0;
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    client_context_t* client_ctx = (client_context_t*)ctx;

    if (resp->status == UVRPC_OK) {
        printf("[CLIENT] Response received, size: %zu\n", resp->result_size);

        if (resp->result_size == 4) {
            int32_t result = *(int32_t*)resp->result;
            printf("[CLIENT] Result: %d\n", result);
        }
    } else {
        printf("[CLIENT] Request failed: %d\n", resp->status);
    }

    client_ctx->received = 1;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    printf("=== UVRPC SAMELOOP RPC Demo ===\n");
    printf("Transport: SAMELOOP (zero-lock, zero-copy)\n");
    printf("Address: sameloop://demo_rpc\n\n");

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create server context */
    server_context_t server_ctx = {0};

    /* Create server configuration */
    printf("[MAIN] Creating server...\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "sameloop://demo_rpc");
    uvrpc_config_set_transport(server_config, UVBUS_TRANSPORT_SAMELOOP);

    /* Create server */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        printf("[MAIN] Failed to create server\n");
        uvrpc_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server created successfully\n");

    /* Register handlers */
    uvrpc_server_register(server, "add", add_handler, &server_ctx);
    uvrpc_server_register(server, "echo", echo_handler, &server_ctx);

    /* Start server */
    printf("[MAIN] Starting server...\n");
    int result = uvrpc_server_start(server);
    if (result != UVRPC_OK) {
        printf("[MAIN] Failed to start server: %d\n", result);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server started successfully\n\n");

    uvrpc_config_free(server_config);

    /* Create client context */
    client_context_t client_ctx = {0};

    /* Create client configuration */
    printf("[MAIN] Creating client...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "sameloop://demo_rpc");
    uvrpc_config_set_transport(client_config, UVBUS_TRANSPORT_SAMELOOP);

    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("[MAIN] Failed to create client\n");
        uvrpc_config_free(client_config);
        uvrpc_server_free(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Client created successfully\n");

    /* Connect with callback */
    result = uvrpc_client_connect_with_callback(client, on_connect, &client_ctx);
    if (result != UVRPC_OK) {
        printf("[MAIN] Failed to initiate connection: %d\n", result);
        uvrpc_config_free(client_config);
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uv_loop_close(&loop);
        return 1;
    }

    uvrpc_config_free(client_config);

    /* Run event loop until client connects */
    printf("[MAIN] Waiting for connection...\n");
    while (g_running && !client_ctx.connected) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    if (!client_ctx.connected) {
        printf("[MAIN] Client connection failed\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uv_loop_close(&loop);
        return 1;
    }

    printf("\n[MAIN] Client connected, sending requests...\n\n");

    /* Send RPC calls */
    int32_t params[2];
    
    /* Test 1: Add operation */
    printf("[MAIN] Test 1: add(10, 20)\n");
    params[0] = 10;
    params[1] = 20;
    client_ctx.received = 0;
    uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, &client_ctx);

    while (g_running && !client_ctx.received) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    /* Test 2: Add operation */
    printf("\n[MAIN] Test 2: add(100, 200)\n");
    params[0] = 100;
    params[1] = 200;
    client_ctx.received = 0;
    uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params), on_response, &client_ctx);

    while (g_running && !client_ctx.received) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    /* Test 3: Echo operation */
    printf("\n[MAIN] Test 3: echo('hello')\n");
    const char* msg = "hello";
    client_ctx.received = 0;
    uvrpc_client_call(client, "echo", (const uint8_t*)msg, strlen(msg), on_response, &client_ctx);

    while (g_running && !client_ctx.received) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    printf("\n[MAIN] All tests completed\n");
    printf("[MAIN] Server processed %d requests\n", server_ctx.request_count);

    /* Cleanup */
    printf("\n[MAIN] Cleaning up...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uv_loop_close(&loop);

    printf("\n=== Demo completed successfully ===\n");
    return 0;
}
