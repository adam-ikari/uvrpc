/**
 * UVRPC INPROC Example - Single Process
 *
 * This example demonstrates how to use INPROC transport for
 * in-process communication. Both server and client run in the same
 * process, sharing the same event loop.
 *
 * INPROC is ideal for:
 * - Modular architecture with inter-module communication
 * - Testing and debugging
 * - High-performance in-process RPC
 *
 * NOTE: INPROC only works when server and client are in the same process.
 * For inter-process communication, use TCP, IPC, or UDP.
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

int main(int argc, char** argv) {
    const char* address = (argc > 1) ? argv[1] : "inproc://test_service";

    printf("[MAIN] UVRPC INPROC Example - Single Process\n");
    printf("[MAIN] Address: %s\n", address);

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create event loop */
    uv_loop_t loop = {0};
    if (uv_loop_init(&loop) != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize event loop\n");
        return 1;
    }

    /* Create server */
    printf("[MAIN] Creating server...\n");

    server_context_t server_ctx = {0};

    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    /* INPROC and SAMELOOP peers meet through a registry the caller owns. */
    uvbus_loop_registry_t* registry = uvbus_loop_registry_new();

    uvrpc_config_set_loop_registry(server_config, registry);
    uvrpc_config_set_address(server_config, address);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "[MAIN] Failed to create server\n");
        uvrpc_config_free(server_config);
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
        return 1;
    }

    /* Register handlers */
    uvrpc_server_register(server, "add", add_handler, &server_ctx);
    uvrpc_server_register(server, "echo", echo_handler, &server_ctx);

    /* Start server */
    int err = uvrpc_server_start(server);
    if (err != UVRPC_OK) {
        fprintf(stderr, "[MAIN] Failed to start server: %d\n", err);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }

    printf("[MAIN] Server started successfully\n");

    /* Create client */
    printf("[MAIN] Creating client...\n");

    client_context_t client_ctx = {0};

    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_loop_registry(client_config, registry);
    uvrpc_config_set_address(client_config, address);
    uvrpc_config_set_max_pending_callbacks(client_config, 64);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "[MAIN] Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        uv_loop_close(&loop);
        return 1;
    }

    /* Connect client */
    printf("[MAIN] Connecting client...\n");

    err = uvrpc_client_connect_with_callback(client, on_connect, &client_ctx);
    if (err != UVRPC_OK) {
        fprintf(stderr, "[MAIN] Failed to initiate connection: %d\n", err);
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(client_config);
        uvrpc_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }

    /* Wait for connection */
    int iterations = 0;
    while (!client_ctx.connected && g_running && iterations < 100) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }

    if (!client_ctx.connected) {
        fprintf(stderr, "[MAIN] Connection timeout\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(client_config);
        uvrpc_config_free(server_config);
        uv_loop_close(&loop);
        return 1;
    }

    /* Send some requests */
    printf("[MAIN] Sending requests...\n");

    for (int i = 0; i < 3 && g_running; i++) {
        client_ctx.received = 0;

        /* Add request */
        int32_t add_params[2] = {10 * (i + 1), 20 * (i + 1)};
        printf("[MAIN] Calling add(%d, %d)\n", add_params[0], add_params[1]);

        err = uvrpc_client_call(client, "add",
                                 (uint8_t*)add_params, sizeof(add_params),
                                 on_response, &client_ctx);

        if (err != UVRPC_OK) {
            fprintf(stderr, "[MAIN] Failed to call add: %d\n", err);
            break;
        }

        /* Wait for response */
        iterations = 0;
        while (!client_ctx.received && g_running && iterations < 50) {
            uv_run(&loop, UV_RUN_DEFAULT);
            uv_stop(&loop);
            iterations++;
        }

        if (!client_ctx.received) {
            fprintf(stderr, "[MAIN] Response timeout\n");
            break;
        }
    }

    /* Cleanup */
    printf("[MAIN] Shutting down...\n");
    printf("[MAIN] Server handled %d requests\n", server_ctx.request_count);

    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    printf("[MAIN] Done\n");
    return 0;
}
