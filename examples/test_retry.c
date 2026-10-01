/**
 * Retry configuration on the client.
 *
 * The retry count lives on the client, not in a global, so two clients in one
 * process can retry differently. This sends a few requests and reports how many
 * came back.
 *
 * Run the server first:
 *   ./rpc_dsl_demo server
 *   ./test_retry [address] [requests] [max_retries]
 */

#include "../include/uvrpc.h"
#include "benchmark_benchmarkservice_api.h"
#include "benchmark_builder.h"
#include "benchmark_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

static int g_connected = 0;
static int g_response_count = 0;

static void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == UVRPC_OK) {
        printf("Connected to server successfully!\n");
        g_connected = 1;
    } else {
        printf("Connection failed: %d\n", (int)status);
    }
}

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    g_response_count++;

    if (resp->status == UVRPC_OK) {
        benchmark_AddResponse_table_t result = benchmark_AddResponse_as_root(resp->result);
        printf("Response #%d for %s (success): result=%lld\n",
               g_response_count, "Add", (long long)benchmark_AddResponse_result(result));
    } else {
        printf("Response #%d for %s (failed: %d)\n",
               g_response_count, "Add", (int)resp->status);
    }
    uvrpc_response_free(resp);
}

int main(int argc, char** argv) {
    const char* address = "tcp://127.0.0.1:5555";
    int num_requests = 5;
    int max_retries = 3;

    if (argc >= 2) address = argv[1];
    if (argc >= 3) num_requests = atoi(argv[2]);
    if (argc >= 4) max_retries = atoi(argv[3]);

    printf("=== Retry Logic Test ===\n");
    printf("Server: %s\n", address);
    printf("Requests: %d\n", num_requests);
    printf("Max retries: %d\n", max_retries);
    printf("========================\n\n");

    uv_loop_t loop;
    uv_loop_init(&loop);

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);

    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uv_loop_close(&loop);
        return 1;
    }

    /* Retry count is per client, and can be changed after creation. */
    uvrpc_client_set_max_retries(client, max_retries);
    printf("Max retries: %d\n", uvrpc_client_get_max_retries(client));

    printf("Creating client...\n");
    if (uvrpc_client_connect_with_callback(client, on_connect, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect\n");
        uvrpc_client_free(client);
        uv_loop_close(&loop);
        return 1;
    }

    printf("Waiting for connection...\n");
    int loop_count = 0;
    while (!g_connected && loop_count < 100) {
        uv_run(&loop, UV_RUN_DEFAULT);
        loop_count++;
    }

    if (!g_connected) {
        fprintf(stderr, "Connection timeout\n");
        uvrpc_client_free(client);
        uv_run(&loop, UV_RUN_NOWAIT);
        uv_loop_close(&loop);
        return 1;
    }

    printf("\nSending %d Add requests with retry logic...\n", num_requests);

    for (int i = 0; i < num_requests; i++) {
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);

        benchmark_AddRequest_start_as_root(&builder);
        benchmark_AddRequest_a_add(&builder, i);
        benchmark_AddRequest_b_add(&builder, i + 1);
        benchmark_AddRequest_end_as_root(&builder);

        size_t size = 0;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        benchmark_AddRequest_table_t request = benchmark_AddRequest_as_root(buf);

        int ret = uvrpc_benchmarkservice_Add(client, on_response, NULL, request);
        if (ret == UVRPC_OK) {
            printf("Sent request #%d\n", i + 1);
        } else {
            printf("Failed to send request #%d (error: %d) - will retry...\n", i + 1, ret);
        }

        flatcc_builder_aligned_free(buf);
        flatcc_builder_clear(&builder);

        uv_run(&loop, UV_RUN_DEFAULT);
    }

    printf("\nWaiting for responses...\n");

    loop_count = 0;
    while (g_response_count < num_requests && loop_count < 1000) {
        uv_run(&loop, UV_RUN_DEFAULT);
        loop_count++;
    }

    printf("\n=== Test Results ===\n");
    printf("Expected responses: %d\n", num_requests);
    printf("Received responses: %d\n", g_response_count);
    printf("Success rate: %.1f%%\n", num_requests ? 100.0 * g_response_count / num_requests : 0.0);
    printf("====================\n");

    uvrpc_client_free(client);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);

    return (g_response_count == num_requests) ? 0 : 1;
}
