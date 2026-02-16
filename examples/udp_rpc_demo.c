/**
 * UVRPC UDP RPC Demo
 * Demonstrates UDP-based request-response RPC calls
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Echo handler */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[Server] Received request: %s\n", req->method);
    uvrpc_request_send_response(req, 0, req->params, req->params_size);
}

/* Add handler */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[Server] Processing add request\n");

    if (req->params_size == 8) {  /* Two int32_t */
        int32_t a = *((int32_t*)req->params);
        int32_t b = *((int32_t*)(req->params + 4));
        int32_t result = a + b;

        printf("[Server] Calculating: %d + %d = %d\n", a, b, result);
        uvrpc_request_send_response(req, 0, (uint8_t*)&result, sizeof(result));
    } else {
        fprintf(stderr, "Invalid params size: %zu\n", req->params_size);
        uvrpc_request_send_response(req, -1, NULL, 0);
    }
}

int run_server(uv_loop_t* loop, const char* address) {
    printf("=== UVRPC UDP RPC Server ===\n");
    printf("Address: %s\n\n", address);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        return 1;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_register(server, "add", add_handler, NULL);

    printf("Starting server...\n");
    int result = uvrpc_server_start(server);
    if (result != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", result);
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        return 1;
    }

    printf("Server running! Press Ctrl+C to stop.\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (g_running) {
        uv_run(loop, UV_RUN_ONCE);
    }

    printf("\nStopping server...\n");
    uvrpc_server_free(server);
    uvrpc_config_free(config);

    return 0;
}

int run_client(uv_loop_t* loop, const char* address) {
    printf("=== UVRPC UDP RPC Client ===\n");
    printf("Address: %s\n\n", address);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        return 1;
    }

    printf("Connecting to server...\n");
    int result = uvrpc_client_connect(client);
    if (result != UVRPC_OK) {
        fprintf(stderr, "Failed to connect: %d\n", result);
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        return 1;
    }

    printf("Connected!\n\n");

    /* Test echo */
    printf("Test 1: Echo\n");
    static int request_count = 0;
    void echo_callback(uvrpc_response_t* resp, void* ctx) {
        (void)ctx;
        if (resp->status == 0) {
            printf("  Echo response: %.*s\n", (int)resp->result_size, resp->result);
        } else {
            printf("  Echo failed: status=%d\n", resp->status);
        }
        request_count++;
    }

    const char* echo_msg = "Hello UDP RPC!";
    uvrpc_client_call(client, "echo",
                      (const uint8_t*)echo_msg, strlen(echo_msg),
                      echo_callback, NULL);

    /* Test add */
    printf("\nTest 2: Add\n");
    void add_callback(uvrpc_response_t* resp, void* ctx) {
        (void)ctx;
        if (resp->status == 0 && resp->result_size == 4) {
            int32_t result = *((int32_t*)resp->result);
            printf("  Add result: %d\n", result);
        } else {
            printf("  Add failed: status=%d\n", resp->status);
        }
        request_count++;
    }

    int32_t add_params[2] = {10, 20};
    uvrpc_client_call(client, "add",
                      (const uint8_t*)add_params, sizeof(add_params),
                      add_callback, NULL);

    /* Wait for responses */
    printf("\nWaiting for responses...\n");
    while (request_count < 2) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(10000);  /* 10ms */
    }

    printf("\nAll tests completed!\n");

    uvrpc_client_free(client);
    uvrpc_config_free(config);

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <server|client> <udp_address>\n", argv[0]);
        printf("Example: %s server udp://127.0.0.1:5555\n", argv[0]);
        printf("         %s client udp://127.0.0.1:5555\n", argv[0]);
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
