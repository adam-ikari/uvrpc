/**
 * @file uvbus_perf_test_simple.c
 * @brief Simplified UVBus Performance Tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <uv.h>
#include <sys/time.h>
#include "../include/uvbus.h"

#define TEST_TCP_PORT 15558
#define TEST_TCP_ADDRESS "tcp://127.0.0.1:15558"

typedef struct {
    int request_count;
    int response_count;
    double total_latency;
    double min_latency;
    double max_latency;
    uint8_t* payload;
    size_t payload_size;
} perf_stats_t;

static perf_stats_t g_stats;

static void server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    /* Echo back the data */
    uvbus_t* server = (uvbus_t*)server_ctx;
    uvbus_send_to(server, data, size, client_ctx);
}

static void client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    g_stats.response_count++;
}

void run_perf_test(const char* test_name, int num_requests, size_t payload_size) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* Setup stats */
    g_stats.request_count = 0;
    g_stats.response_count = 0;
    g_stats.total_latency = 0.0;
    g_stats.min_latency = 999999.0;
    g_stats.max_latency = 0.0;
    g_stats.payload_size = payload_size;
    g_stats.payload = malloc(payload_size);
    memset(g_stats.payload, 'A', payload_size);

    /* Create server */
    uvbus_config_t* server_config = uvbus_config_new();
    uvbus_config_set_loop(server_config, &loop);
    uvbus_config_set_transport(server_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(server_config, TEST_TCP_ADDRESS);
    uvbus_config_set_recv_callback(server_config, server_recv, NULL);

    uvbus_t* server = uvbus_server_new(server_config);
    if (!server) {
        printf("ERROR: Failed to create server\n");
        return;
    }

    uvbus_error_t result = uvbus_listen(server);
    if (result != UVBUS_OK) {
        printf("ERROR: Failed to listen: %d\n", result);
        return;
    }

    /* Create client */
    uvbus_config_t* client_config = uvbus_config_new();
    uvbus_config_set_loop(client_config, &loop);
    uvbus_config_set_transport(client_config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(client_config, TEST_TCP_ADDRESS);
    uvbus_config_set_recv_callback(client_config, client_recv, NULL);

    uvbus_t* client = uvbus_client_new(client_config);
    if (!client) {
        printf("ERROR: Failed to create client\n");
        return;
    }

    result = uvbus_connect(client);
    if (result != UVBUS_OK) {
        printf("ERROR: Failed to connect: %d\n", result);
    }

    /* Wait for connection */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);
    }

    if (!uvbus_is_connected(client)) {
        printf("ERROR: Client not connected\n");
        return;
    }

    printf("\n========================================\n");
    printf("  %s\n", test_name);
    printf("========================================\n");
    printf("Requests: %d\n", num_requests);
    printf("Payload: %zu bytes\n", payload_size);

    /* Warmup */
    printf("Warming up...\n");
    for (int i = 0; i < 100; i++) {
        uvbus_client_send(client, g_stats.payload, payload_size);
        uv_run(&loop, UV_RUN_NOWAIT);
    }
    usleep(100000);

    /* Measure */
    printf("Testing...\n");
    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < num_requests; i++) {
        uvbus_error_t send_result = uvbus_client_send(client, g_stats.payload, payload_size);
        if (send_result == UVBUS_OK) {
            g_stats.request_count++;
        }

        if (i % 100 == 0) {
            uv_run(&loop, UV_RUN_NOWAIT);
            usleep(1000);
        }
    }

    /* Wait for responses */
    int retry = 0;
    while (g_stats.response_count < g_stats.request_count && retry < 5000) {
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(1000);
        retry++;
    }

    gettimeofday(&end, NULL);

    /* Calculate results */
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    double throughput = g_stats.response_count / elapsed;
    double avg_latency = (elapsed * 1000.0) / g_stats.response_count;

    printf("\nResults:\n");
    printf("  Sent: %d\n", g_stats.request_count);
    printf("  Received: %d\n", g_stats.response_count);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Throughput: %.0f req/s\n", throughput);
    printf("  Avg latency: %.3f ms\n", avg_latency);
    printf("========================================\n\n");

    /* Cleanup */
    uvbus_free(client);
    uvbus_free(server);
    uvbus_config_free(client_config);
    uvbus_config_free(server_config);
    free(g_stats.payload);

    uv_loop_close(&loop);
}

int main(void) {
    printf("========================================\n");
    printf("  UVBus Performance Tests\n");
    printf("========================================\n");

    run_perf_test("Small Payload (64 bytes)", 10000, 64);
    run_perf_test("Medium Payload (1KB)", 5000, 1024);
    run_perf_test("Large Payload (64KB)", 500, 65536);

    printf("========================================\n");
    printf("  Performance Tests Complete\n");
    printf("========================================\n");

    return 0;
}
