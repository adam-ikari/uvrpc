/**
 * Performance test for generated code
 * No waiting version - pure event loop processing
 */

#include "uvrpc.h"
#include "echoservice_gen.h"
#include "echoservice_gen_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

/* Performance statistics */
typedef struct {
    uint64_t total_ops;
    uint64_t successful_ops;
    uint64_t failed_ops;
    uint64_t total_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
} perf_stats_t;

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_perf_stats(const perf_stats_t* stats, const char* test_name) {
    double avg_latency_us = stats->successful_ops > 0 ?
        (double)stats->total_latency_ns / stats->successful_ops / 1000.0 : 0.0;
    double ops_per_sec = stats->total_latency_ns > 0 ?
        (double)stats->successful_ops * 1000000000.0 / stats->total_latency_ns : 0.0;

    printf("\n========== %s Performance ==========\n", test_name);
    printf("Total operations:     %" PRIu64 "\n", stats->total_ops);
    printf("Successful:           %" PRIu64 "\n", stats->successful_ops);
    printf("Failed:               %" PRIu64 "\n", stats->failed_ops);
    printf("Success rate:         %.2f%%\n",
           stats->total_ops > 0 ? (double)stats->successful_ops / stats->total_ops * 100.0 : 0.0);
    printf("Average latency:      %.2f us\n", avg_latency_us);
    printf("Min latency:          %.2f us\n", stats->min_latency_ns / 1000.0);
    printf("Max latency:          %.2f us\n", stats->max_latency_ns / 1000.0);
    printf("Throughput:           %.2f ops/sec\n", ops_per_sec);
    printf("==========================================\n");
}

/* Test echo method performance */
static void test_echo_perf(uvrpc_client_t* client, uv_loop_t* loop, int iterations) {
    perf_stats_t stats = {0};
    stats.min_latency_ns = UINT64_MAX;

    EchoService_echo_Request_t request = {0};
    request.message = "Hello, UVRPC!";

    /* Send all requests first */
    uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(iterations * sizeof(uvrpc_async_t*));
    uint64_t* start_times = (uint64_t*)malloc(iterations * sizeof(uint64_t));
    EchoService_echo_Response_t* responses = (EchoService_echo_Response_t*)malloc(iterations * sizeof(EchoService_echo_Response_t));
    int* completed = (int*)calloc(iterations, sizeof(int));

    /* Start all async calls */
    for (int i = 0; i < iterations; i++) {
        responses[i] = (EchoService_echo_Response_t){0};
        asyncs[i] = uvrpc_async_create(loop);
        if (!asyncs[i]) {
            stats.failed_ops++;
            completed[i] = 1;
            continue;
        }

        start_times[i] = get_time_ns();
        int rc = EchoService_echo_Async(client, &request, asyncs[i]);
        if (rc != UVRPC_OK) {
            stats.failed_ops++;
            uvrpc_async_free(asyncs[i]);
            asyncs[i] = NULL;
            completed[i] = 1;
        }
    }

    /* Run event loop until all requests complete */
    int all_done = 0;
    int loop_count = 0;
    while (!all_done && loop_count < 100000) {
        uv_run(loop, UV_RUN_NOWAIT);
        loop_count++;
        
        all_done = 1;
        for (int i = 0; i < iterations; i++) {
            if (!completed[i] && asyncs[i]) {
                const uvrpc_async_result_t* result = uvrpc_async_await(asyncs[i]);
                if (result && result->status != 0) {
                    completed[i] = 1;
                    uint64_t end = get_time_ns();
                    uint64_t latency = end - start_times[i];
                    
                    stats.total_ops++;
                    stats.total_latency_ns += latency;

                    if (latency < stats.min_latency_ns) stats.min_latency_ns = latency;
                    if (latency > stats.max_latency_ns) stats.max_latency_ns = latency;

                    if (result->status == UVRPC_OK) {
                        EchoService_echo_Await(result, &responses[i]);
                        stats.successful_ops++;
                        EchoService_echo_FreeResponse(&responses[i]);
                    } else {
                        stats.failed_ops++;
                    }
                    
                    uvrpc_async_free(asyncs[i]);
                    asyncs[i] = NULL;
                } else {
                    all_done = 0;
                }
            }
        }
    }

    /* Free remaining asyncs */
    for (int i = 0; i < iterations; i++) {
        if (asyncs[i]) {
            uvrpc_async_free(asyncs[i]);
        }
    }

    free(asyncs);
    free(start_times);
    free(responses);
    free(completed);

    print_perf_stats(&stats, "Echo");
}

/* Test add method performance */
static void test_add_perf(uvrpc_client_t* client, uv_loop_t* loop, int iterations) {
    perf_stats_t stats = {0};
    stats.min_latency_ns = UINT64_MAX;

    EchoService_add_Request_t request = {0};
    request.a = 1.5;
    request.b = 2.5;

    /* Send all requests first */
    uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(iterations * sizeof(uvrpc_async_t*));
    uint64_t* start_times = (uint64_t*)malloc(iterations * sizeof(uint64_t));
    EchoService_add_Response_t* responses = (EchoService_add_Response_t*)malloc(iterations * sizeof(EchoService_add_Response_t));
    int* completed = (int*)calloc(iterations, sizeof(int));

    /* Start all async calls */
    for (int i = 0; i < iterations; i++) {
        responses[i] = (EchoService_add_Response_t){0};
        asyncs[i] = uvrpc_async_create(loop);
        if (!asyncs[i]) {
            stats.failed_ops++;
            completed[i] = 1;
            continue;
        }

        start_times[i] = get_time_ns();
        int rc = EchoService_add_Async(client, &request, asyncs[i]);
        if (rc != UVRPC_OK) {
            stats.failed_ops++;
            uvrpc_async_free(asyncs[i]);
            asyncs[i] = NULL;
            completed[i] = 1;
        }
    }

    /* Run event loop until all requests complete */
    int all_done = 0;
    int loop_count = 0;
    while (!all_done && loop_count < 100000) {
        uv_run(loop, UV_RUN_NOWAIT);
        loop_count++;
        
        all_done = 1;
        for (int i = 0; i < iterations; i++) {
            if (!completed[i] && asyncs[i]) {
                const uvrpc_async_result_t* result = uvrpc_async_await(asyncs[i]);
                if (result && result->status != 0) {
                    completed[i] = 1;
                    uint64_t end = get_time_ns();
                    uint64_t latency = end - start_times[i];
                    
                    stats.total_ops++;
                    stats.total_latency_ns += latency;

                    if (latency < stats.min_latency_ns) stats.min_latency_ns = latency;
                    if (latency > stats.max_latency_ns) stats.max_latency_ns = latency;

                    if (result->status == UVRPC_OK) {
                        EchoService_add_Await(result, &responses[i]);
                        stats.successful_ops++;
                        EchoService_add_FreeResponse(&responses[i]);
                    } else {
                        stats.failed_ops++;
                    }
                    
                    uvrpc_async_free(asyncs[i]);
                    asyncs[i] = NULL;
                } else {
                    all_done = 0;
                }
            }
        }
    }

    /* Free remaining asyncs */
    for (int i = 0; i < iterations; i++) {
        if (asyncs[i]) {
            uvrpc_async_free(asyncs[i]);
        }
    }

    free(asyncs);
    free(start_times);
    free(responses);
    free(completed);

    print_perf_stats(&stats, "Add");
}

/* Test get_info method performance */
static void test_get_info_perf(uvrpc_client_t* client, uv_loop_t* loop, int iterations) {
    perf_stats_t stats = {0};
    stats.min_latency_ns = UINT64_MAX;

    EchoService_getInfo_Request_t request;

    /* Send all requests first */
    uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(iterations * sizeof(uvrpc_async_t*));
    uint64_t* start_times = (uint64_t*)malloc(iterations * sizeof(uint64_t));
    EchoService_getInfo_Response_t* responses = (EchoService_getInfo_Response_t*)malloc(iterations * sizeof(EchoService_getInfo_Response_t));
    int* completed = (int*)calloc(iterations, sizeof(int));

    /* Start all async calls */
    for (int i = 0; i < iterations; i++) {
        responses[i] = (EchoService_getInfo_Response_t){0};
        asyncs[i] = uvrpc_async_create(loop);
        if (!asyncs[i]) {
            stats.failed_ops++;
            completed[i] = 1;
            continue;
        }

        start_times[i] = get_time_ns();
        int rc = EchoService_getInfo_Async(client, &request, asyncs[i]);
        if (rc != UVRPC_OK) {
            stats.failed_ops++;
            uvrpc_async_free(asyncs[i]);
            asyncs[i] = NULL;
            completed[i] = 1;
        }
    }

    /* Run event loop until all requests complete */
    int all_done = 0;
    int loop_count = 0;
    while (!all_done && loop_count < 100000) {
        uv_run(loop, UV_RUN_NOWAIT);
        loop_count++;
        
        all_done = 1;
        for (int i = 0; i < iterations; i++) {
            if (!completed[i] && asyncs[i]) {
                const uvrpc_async_result_t* result = uvrpc_async_await(asyncs[i]);
                if (result && result->status != 0) {
                    completed[i] = 1;
                    uint64_t end = get_time_ns();
                    uint64_t latency = end - start_times[i];
                    
                    stats.total_ops++;
                    stats.total_latency_ns += latency;

                    if (latency < stats.min_latency_ns) stats.min_latency_ns = latency;
                    if (latency > stats.max_latency_ns) stats.max_latency_ns = latency;

                    if (result->status == UVRPC_OK) {
                        EchoService_getInfo_Await(result, &responses[i]);
                        stats.successful_ops++;
                        EchoService_getInfo_FreeResponse(&responses[i]);
                    } else {
                        stats.failed_ops++;
                    }
                    
                    uvrpc_async_free(asyncs[i]);
                    asyncs[i] = NULL;
                } else {
                    all_done = 0;
                }
            }
        }
    }

    /* Free remaining asyncs */
    for (int i = 0; i < iterations; i++) {
        if (asyncs[i]) {
            uvrpc_async_free(asyncs[i]);
        }
    }

    free(asyncs);
    free(start_times);
    free(responses);
    free(completed);

    print_perf_stats(&stats, "GetInfo");
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:6666";
    int iterations = (argc > 2) ? atoi(argv[2]) : 1000;

    printf("========================================\n");
    printf("Generated Code Performance Test\n");
    printf("========================================\n");
    printf("Server address: %s\n", server_addr);
    printf("Iterations: %d\n\n", iterations);

    /* Create libuv event loop */
    uv_loop_t* loop = uv_default_loop();

    /* Create ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* Create client config */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* Create client */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* Connect to server */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }
    printf("Connected to server\n\n");

    /* Run event loop to let connection establish */
    for (int i = 0; i < 100; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* Run performance tests */
    test_echo_perf(client, loop, iterations);
    test_add_perf(client, loop, iterations);
    test_get_info_perf(client, loop, iterations);

    printf("\n========================================\n");
    printf("All tests completed!\n");
    printf("========================================\n");

    /* Cleanup */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    return 0;
}