#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int requests_sent = 0;
static int responses_received = 0;
static const int REQUEST_COUNT = 1000;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    responses_received++;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
}

void on_connect(int status, void* ctx) {
    printf("[CONNECT] Status: %d\n", status);
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    responses_received++;
}

int main() {
    uv_loop_t loop;
    int rc = uv_loop_init(&loop);
    if (rc != 0) {
        fprintf(stderr, "Failed to init loop: %d\n", rc);
        return 1;
    }

    printf("=== INPROC Simple Performance Test ===\n");
    printf("Requests: %d\n", REQUEST_COUNT);
    printf("");

    /* Create server */
    printf("Creating server...\n");
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://simple_test");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }
    printf("Server created\n");

    const char* echo_method = "echo";
    uvrpc_server_register(server, echo_method, echo_handler, NULL);
    printf("Handler registered\n");

    int ret = uvrpc_server_start(server);
    printf("Server start result: %d\n", ret);

    if (ret != UVRPC_OK) {
        printf("Failed to start server: %d\n", ret);
        return 1;
    }
    printf("Server started\n");

    /* Create client */
    printf("\nCreating client...\n");
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://simple_test");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    printf("Client created\n");

    ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    printf("Client connect result: %d\n", ret);

    if (ret != UVRPC_OK) {
        printf("Failed to connect client: %d\n", ret);
        return 1;
    }

    /* Wait for connection */
    printf("Waiting for connection...\n");
    for (int i = 0; i < 50; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    printf("Connection established\n");

    /* Send requests one by one */
    printf("\nSending %d requests...\n", REQUEST_COUNT);
    const char* test_data = "Hello!";
    size_t data_len = strlen(test_data);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < REQUEST_COUNT; i++) {
        ret = uvrpc_client_call(client, echo_method,
                                (uint8_t*)test_data, data_len,
                                on_response, NULL);
        if (ret != UVRPC_OK) {
            printf("Failed to send request %d: %d\n", i, ret);
            break;
        }
        requests_sent++;

        /* Process events after each request */
        uv_run(&loop, UV_RUN_ONCE);
    }

    /* Wait for remaining responses */
    printf("Waiting for responses...\n");
    for (int i = 0; i < 1000 && responses_received < requests_sent; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n=== Results ===\n");
    printf("Requests sent: %d\n", requests_sent);
    printf("Responses received: %d\n", responses_received);
    printf("Time: %.3f seconds\n", elapsed);
    if (elapsed > 0) {
        printf("Throughput: %.0f ops/s\n", requests_sent / elapsed);
        printf("Average latency: %.3f ms\n", (elapsed * 1000) / requests_sent);
    }

    /* Cleanup */
    printf("\nCleaning up...\n");
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    printf("Test completed\n");
    return 0;
}