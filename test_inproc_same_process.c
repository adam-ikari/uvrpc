#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int request_received = 0;
static volatile int response_received = 0;
static volatile int client_connected = 0;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("[HANDLER] Received request: method=%s, msgid=%lu\n", req->method, req->msgid);
    fflush(stdout);
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    request_received = 1;
}

void on_connect(int status, void* ctx) {
    printf("[CLIENT] Connected: status=%d\n", status);
    fflush(stdout);
    if (status == 0) {
        client_connected = 1;
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    printf("[RESPONSE] Status=%d, msgid=%lu, size=%zu\n", resp->status, resp->msgid, resp->result_size);
    fflush(stdout);
    response_received = 1;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    printf("=== INPROC Same Process Test ===\n");

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://test");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);

    int ret = uvrpc_server_start(server);
    printf("Server start result: %d\n", ret);

    if (ret != UVRPC_OK) {
        printf("Failed to start server\n");
        return 1;
    }

    printf("Server started successfully\n");

    /* Create client in same process */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://test");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    printf("Client connect result: %d\n", ret);

    if (ret != UVRPC_OK) {
        printf("Failed to connect client\n");
        return 1;
    }

    /* Run loop to establish connection */
    for (int i = 0; i < 50 && !client_connected; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    if (!client_connected) {
        printf("Client failed to connect\n");
        return 1;
    }

    printf("Client connected successfully\n");

    /* Make a request */
    const char* test_data = "Hello, INPROC!";
    ret = uvrpc_client_call(client, "echo", (uint8_t*)test_data, strlen(test_data), on_response, NULL);
    printf("Call result: %d\n", ret);

    if (ret != UVRPC_OK) {
        printf("Failed to make call\n");
        return 1;
    }

    /* Run loop to process request/response */
    for (int i = 0; i < 50 && !response_received; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    if (!response_received) {
        printf("No response received\n");
        return 1;
    }

    printf("=== Test PASSED ===\n");

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    return 0;
}