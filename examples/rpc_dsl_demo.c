/**
 * UVRPC DSL Demo
 * Demonstrates using FlatBuffers-based RPC DSL for type-safe API definitions
 */

#include "../include/uvrpc.h"
#include "../generated/rpc_api_builder.h"
#include "../generated/rpc_api_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Service handler context */
typedef struct {
    const char* name;
    int call_count;
} service_context_t;

/* Math service handlers */

void add_handler(uvrpc_request_t* req, void* ctx) {
    service_context_t* svc = (service_context_t*)ctx;
    svc->call_count++;

    printf("[MathService::Add] Call #%d\n", svc->call_count);

    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;

        printf("[MathService::Add] %d + %d = %d\n", a, b, result);

        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        printf("[MathService::Add] Invalid params size: %zu\n", req->params_size);
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

void multiply_handler(uvrpc_request_t* req, void* ctx) {
    service_context_t* svc = (service_context_t*)ctx;
    svc->call_count++;

    printf("[MathService::Multiply] Call #%d\n", svc->call_count);

    if (req->params_size >= 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a * b;

        printf("[MathService::Multiply] %d * %d = %d\n", a, b, result);

        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    } else {
        printf("[MathService::Multiply] Invalid params size: %zu\n", req->params_size);
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
    }
}

/* Client example */

static volatile int g_running = 1;
static volatile int g_connected = 0;

void on_connect(int status, void* ctx) {
    (void)ctx;
    printf("[Client] Connected: %s\n", status == 0 ? "OK" : "FAILED");
    if (status == 0) {
        g_connected = 1;
    } else {
        g_running = 0;
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    printf("[Client] Response: status=%d, msgid=%u, result_size=%zu\n",
           resp->status, resp->msgid, resp->result_size);

    if (resp->status == UVRPC_OK && resp->result && resp->result_size >= 4) {
        int32_t result = *(int32_t*)resp->result;
        printf("[Client] Result: %d\n", result);
    }
    g_running = 0;
}

/* API wrapper functions */

int math_add(uvrpc_client_t* client, int32_t a, int32_t b, uvrpc_callback_t callback, void* ctx) {
    int32_t params[2] = {a, b};
    return uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), callback, ctx);
}

int math_multiply(uvrpc_client_t* client, int32_t a, int32_t b, uvrpc_callback_t callback, void* ctx) {
    int32_t params[2] = {a, b};
    return uvrpc_client_call(client, "Multiply", (uint8_t*)params, sizeof(params), callback, ctx);
}

/* Server example */

int run_server(uv_loop_t* loop, const char* address) {
    printf("=== MathService Server ===\n");
    printf("Address: %s\n\n", address);

    service_context_t math_ctx = {
        .name = "MathService",
        .call_count = 0
    };

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_config_free(config);

    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    /* Register service methods */
    uvrpc_server_register(server, "Add", add_handler, &math_ctx);
    uvrpc_server_register(server, "Multiply", multiply_handler, &math_ctx);

    printf("Registered methods: Add, Multiply\n\n");

    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        return 1;
    }

    printf("Server running. Press Ctrl+C to stop.\n");
    uv_run(loop, UV_RUN_DEFAULT);

    printf("\n[Server] Shutdown\n");
    printf("Total calls: %d\n", math_ctx.call_count);

    uvrpc_server_free(server);
    return 0;
}

/* Client example */

int run_client(uv_loop_t* loop, const char* address) {
    printf("=== MathService Client ===\n");
    printf("Address: %s\n\n", address);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);

    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    uvrpc_client_connect_with_callback(client, on_connect, NULL);

    /* Wait for connection */
    int iterations = 0;
    while (!g_connected && g_running && iterations < 100) {
        uv_run(loop, UV_RUN_ONCE);
        iterations++;
    }

    if (!g_connected) {
        fprintf(stderr, "Connection timeout\n");
        uvrpc_client_free(client);
        return 1;
    }

    /* Call MathService API */
    printf("[Client] Calling MathService.Add(10, 20)...\n");
    math_add(client, 10, 20, on_response, NULL);

    /* Wait for response */
    iterations = 0;
    while (g_running && iterations < 50) {
        uv_run(loop, UV_RUN_ONCE);
        iterations++;
    }

    uvrpc_client_free(client);
    printf("\n[Client] Done\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <server|client> <address>\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s server tcp://127.0.0.1:5555\n", argv[0]);
        printf("  %s client tcp://127.0.0.1:5555\n", argv[0]);
        printf("  %s server ipc:///tmp/math.sock\n", argv[0]);
        printf("  %s client ipc:///tmp/math.sock\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];
    const char* address = argv[2];

    uv_loop_t loop;
    uv_loop_init(&loop);

    int result = 0;
    if (strcmp(mode, "server") == 0) {
        result = run_server(&loop, address);
    } else if (strcmp(mode, "client") == 0) {
        result = run_client(&loop, address);
    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        result = 1;
    }

    uv_loop_close(&loop);
    return result;
}