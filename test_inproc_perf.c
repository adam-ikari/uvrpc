#include "include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile int requests_completed = 0;
static const int REQUEST_COUNT = 100000;

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    uvrpc_request_send_response(req, UVRPC_OK, req->params, req->params_size);
    requests_completed++;
}

void on_connect(int status, void* ctx) {
    if (status != 0) {
        fprintf(stderr, "Connection failed: %d\n", status);
    }
}

void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        requests_completed++;
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    printf("=== INPROC Performance Test ===\n");
    printf("Requests: %d\n", REQUEST_COUNT);
    printf("");

    /* Create server */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://perf_test");
    uvrpc_config_set_comm_type(server_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }

    uvrpc_server_register(server, "echo", echo_handler, NULL);

    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        printf("Failed to start server: %d\n", ret);
        return 1;
    }

    printf("Server started\n");

    /* Create client */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://perf_test");
    uvrpc_config_set_comm_type(client_config, UVRPC_COMM_SERVER_CLIENT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    ret = uvrpc_client_connect_with_callback(client, on_connect, NULL);
    if (ret != UVRPC_OK) {
        printf("Failed to connect client: %d\n", ret);
        return 1;
    }

    /* Wait for connection */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    printf("Client connected\n");
    printf("Starting performance test...\n");

    /* Start timer */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Send requests */
    const char* test_data = "Hello, INPROC!";
    for (int i = 0; i < REQUEST_COUNT; i++) {
        ret = uvrpc_client_call(client, "echo",
                                (uint8_t*)test_data, strlen(test_data),
                                on_response, NULL);
        if (ret != UVRPC_OK) {
            printf("Failed to send request %d: %d\n", i, ret);
            break;
        }

        /* Process some events to prevent queue overflow */
        if (i % 1000 == 0) {
            for (int j = 0; j < 10; j++) {
                uv_run(&loop, UV_RUN_NOWAIT);
            }
        }
    }

    /* Wait for all responses */
    int iterations = 0;
    while (requests_completed < REQUEST_COUNT && iterations < 10000) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
    }

    /* Stop timer */
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Completed %d/%d requests\n", requests_completed, REQUEST_COUNT);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.0f ops/s\n", REQUEST_COUNT / elapsed);
    printf("Average latency: %.3f ms\n", (elapsed * 1000) / REQUEST_COUNT);

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);

    return 0;
}