/**
 * @file uvbus_perf_test.c
 * @brief UVBus Performance Tests
 * 
 * Performance tests for UVBus TCP transport including:
 * - Throughput tests
 * - Latency tests
 * - Different payload sizes
 * - Concurrent connections
 * 
 * @author UVRPC Team
 * @date 2026-02-22
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <uv.h>
#include <sys/time.h>
#include "../include/uvbus.h"
#include "../include/uvbus_config.h"

/* Test configuration */
#define TEST_TCP_PORT 15556
#define TEST_TCP_ADDRESS "tcp://127.0.0.1:15556"
#define TEST_TIMEOUT_MS 10000
#define TEST_WARMUP_REQUESTS 100
#define TEST_MIN_REQUESTS 1000
#define TEST_MAX_REQUESTS 100000

/* Performance test parameters */
typedef struct {
    int num_requests;
    int payload_size;
    int num_clients;
    int warmup_requests;
} perf_params_t;

/* Performance results */
typedef struct {
    double throughput_rps;       /* Requests per second */
    double avg_latency_ms;       /* Average latency in milliseconds */
    double min_latency_ms;       /* Minimum latency */
    double max_latency_ms;       /* Maximum latency */
    double p50_latency_ms;       /* 50th percentile latency */
    double p95_latency_ms;       /* 95th percentile latency */
    double p99_latency_ms;       /* 99th percentile latency */
    double total_time_sec;       /* Total test time */
    int total_bytes;             /* Total bytes transferred */
    double throughput_mbps;      /* Throughput in Mbps */
} perf_results_t;

/* Test context */
typedef struct {
    uv_loop_t* loop;
    uvbus_t* server;
    uvbus_t** clients;
    int num_clients;
    
    int request_sent;
    int request_received;
    int complete;
    
    double* latencies;
    int latency_count;
    
    perf_params_t params;
    perf_results_t results;
    
    struct timeval start_time;
    struct timeval end_time;
} perf_context_t;

/* Helper macros */
#define INFO(msg, ...) printf("[INFO] " msg "\n", ##__VA_ARGS__)
#define ERROR(msg, ...) fprintf(stderr, "[ERROR] " msg "\n", ##__VA_ARGS__)

/* Timer functions */
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Comparison function for qsort */
static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Calculate percentile */
static double calculate_percentile(double* data, int count, double percentile) {
    if (count == 0) return 0.0;
    
    qsort(data, count, sizeof(double), compare_double);
    
    int index = (int)(count * percentile / 100.0);
    if (index >= count) index = count - 1;
    
    return data[index];
}

/* Callback functions */
static void perf_server_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    perf_context_t* ctx = (perf_context_t*)server_ctx;
    
    /* Echo back the data */
    uvbus_send_to(ctx->server, data, size, client_ctx);
    
    ctx->request_received++;
}

static void perf_client_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    perf_context_t* ctx = (perf_context_t*)server_ctx;
    
    double now = get_time_ms();
    double latency = now - ctx->latencies[ctx->latency_count];
    ctx->latencies[ctx->latency_count] = latency;
    ctx->latency_count++;
    
    ctx->request_received++;
}

/* Test setup */
static int setup_server(perf_context_t* ctx) {
    uvbus_config_t* config = uvbus_config_new();
    if (!config) {
        ERROR("Failed to create server config");
        return -1;
    }
    
    uvbus_config_set_loop(config, ctx->loop);
    uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
    uvbus_config_set_address(config, TEST_TCP_ADDRESS);
    uvbus_config_set_recv_callback(config, perf_server_recv, ctx);
    
    ctx->server = uvbus_server_new(config);
    if (!ctx->server) {
        ERROR("Failed to create server");
        uvbus_config_free(config);
        return -1;
    }
    
    uvbus_error_t result = uvbus_listen(ctx->server);
    if (result != UVBUS_OK) {
        ERROR("Failed to listen: %d", result);
        uvbus_free(ctx->server);
        uvbus_config_free(config);
        return -1;
    }
    
    uvbus_config_free(config);
    INFO("Server listening on %s", TEST_TCP_ADDRESS);
    
    return 0;
}

static int setup_clients(perf_context_t* ctx) {
    ctx->clients = (uvbus_t**)malloc(ctx->num_clients * sizeof(uvbus_t*));
    if (!ctx->clients) {
        ERROR("Failed to allocate clients array");
        return -1;
    }
    
    for (int i = 0; i < ctx->num_clients; i++) {
        uvbus_config_t* config = uvbus_config_new();
        if (!config) {
            ERROR("Failed to create client config");
            return -1;
        }
        
        uvbus_config_set_loop(config, ctx->loop);
        uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
        uvbus_config_set_address(config, TEST_TCP_ADDRESS);
        uvbus_config_set_recv_callback(config, perf_client_recv, ctx);
        
        ctx->clients[i] = uvbus_client_new(config);
        if (!ctx->clients[i]) {
            ERROR("Failed to create client %d", i);
            uvbus_config_free(config);
            return -1;
        }
        
        uvbus_error_t result = uvbus_connect(ctx->clients[i]);
        if (result != UVBUS_OK) {
            ERROR("Failed to connect client %d: %d", i, result);
        }
        
        uvbus_config_free(config);
    }
    
    /* Wait for connections to establish */
    INFO("Waiting for %d clients to connect...", ctx->num_clients);
    for (int i = 0; i < 100; i++) {
        uv_run(ctx->loop, UV_RUN_NOWAIT);
        usleep(10000); /* 10ms */
    }
    
    INFO("Clients connected");
    return 0;
}

/* Send test requests */
static void send_requests(perf_context_t* ctx, uint8_t* payload, int payload_size) {
    double* send_times = (double*)malloc(ctx->params.num_requests * sizeof(double));
    if (!send_times) {
        ERROR("Failed to allocate send times array");
        return;
    }
    
    gettimeofday(&ctx->start_time, NULL);
    
    /* Warmup phase */
    if (ctx->params.warmup_requests > 0) {
        INFO("Warmup: sending %d requests...", ctx->params.warmup_requests);
        for (int i = 0; i < ctx->params.warmup_requests; i++) {
            int client_idx = i % ctx->num_clients;
            uvbus_client_send(ctx->clients[client_idx], payload, payload_size);
            uv_run(ctx->loop, UV_RUN_NOWAIT);
            if (i % 100 == 0) {
                usleep(1000);
            }
        }
        
        /* Wait for warmup to complete */
        int retry = 0;
        while (ctx->request_received < ctx->params.warmup_requests && retry < 1000) {
            uv_run(ctx->loop, UV_RUN_NOWAIT);
            usleep(1000);
            retry++;
        }
        ctx->request_received = 0;
        INFO("Warmup complete");
    }
    
    /* Measure phase */
    INFO("Starting performance test: %d requests, %d bytes payload", 
         ctx->params.num_requests, payload_size);
    
    for (int i = 0; i < ctx->params.num_requests; i++) {
        send_times[i] = get_time_ms();
        int client_idx = i % ctx->num_clients;
        uvbus_error_t result = uvbus_client_send(ctx->clients[client_idx], payload, payload_size);
        
        if (result != UVBUS_OK) {
            /* Wait a bit if buffer is full */
            usleep(100);
            i--; /* Retry */
            continue;
        }
        
        uv_run(ctx->loop, UV_RUN_NOWAIT);
        
        /* Control send rate if needed */
        if (payload_size > 1024) {
            usleep(10);
        }
    }
    
    /* Wait for all responses */
    INFO("Waiting for responses...");
    int retry = 0;
    while (ctx->request_received < ctx->params.num_requests && retry < 10000) {
        uv_run(ctx->loop, UV_RUN_NOWAIT);
        usleep(1000);
        retry++;
    }
    
    gettimeofday(&ctx->end_time, NULL);
    
    INFO("Received %d/%d responses", ctx->request_received, ctx->params.num_requests);
    
    free(send_times);
    ctx->complete = 1;
}

/* Calculate performance metrics */
static void calculate_results(perf_context_t* ctx) {
    struct timeval diff;
    timersub(&ctx->end_time, &ctx->start_time, &diff);
    
    double total_time_sec = diff.tv_sec + diff.tv_usec / 1000000.0;
    double total_time_ms = total_time_sec * 1000.0;
    
    int completed_requests = ctx->latency_count;
    
    if (completed_requests == 0) {
        ERROR("No requests completed");
        return;
    }
    
    /* Calculate statistics */
    double min_latency = ctx->latencies[0];
    double max_latency = ctx->latencies[0];
    double total_latency = 0.0;
    
    for (int i = 0; i < completed_requests; i++) {
        double lat = ctx->latencies[i];
        if (lat < min_latency) min_latency = lat;
        if (lat > max_latency) max_latency = lat;
        total_latency += lat;
    }
    
    ctx->results.total_time_sec = total_time_sec;
    ctx->results.throughput_rps = completed_requests / total_time_sec;
    ctx->results.avg_latency_ms = total_latency / completed_requests;
    ctx->results.min_latency_ms = min_latency;
    ctx->results.max_latency_ms = max_latency;
    ctx->results.p50_latency_ms = calculate_percentile(ctx->latencies, completed_requests, 50.0);
    ctx->results.p95_latency_ms = calculate_percentile(ctx->latencies, completed_requests, 95.0);
    ctx->results.p99_latency_ms = calculate_percentile(ctx->latencies, completed_requests, 99.0);
    
    /* Calculate throughput in Mbps */
    ctx->results.total_bytes = completed_requests * 2 * ctx->params.payload_size; /* Request + Response */
    ctx->results.throughput_mbps = (ctx->results.total_bytes * 8.0) / (total_time_sec * 1000000.0);
}

/* Print results */
static void print_results(const char* test_name, const perf_results_t* results, const perf_params_t* params) {
    printf("\n========================================\n");
    printf("  %s\n", test_name);
    printf("========================================\n");
    printf("Test Parameters:\n");
    printf("  Requests: %d\n", params->num_requests);
    printf("  Payload size: %d bytes\n", params->payload_size);
    printf("  Clients: %d\n", params->num_clients);
    printf("\nResults:\n");
    printf("  Total time: %.3f seconds\n", results->total_time_sec);
    printf("  Throughput: %.0f req/s\n", results->throughput_rps);
    printf("  Data rate: %.3f Mbps\n", results->throughput_mbps);
    printf("  Total bytes: %d\n", results->total_bytes);
    printf("\nLatency:\n");
    printf("  Average: %.3f ms\n", results->avg_latency_ms);
    printf("  Min: %.3f ms\n", results->min_latency_ms);
    printf("  Max: %.3f ms\n", results->max_latency_ms);
    printf("  P50: %.3f ms\n", results->p50_latency_ms);
    printf("  P95: %.3f ms\n", results->p95_latency_ms);
    printf("  P99: %.3f ms\n", results->p99_latency_ms);
    printf("========================================\n\n");
}

/* Cleanup */
static void cleanup(perf_context_t* ctx) {
    if (ctx->clients) {
        for (int i = 0; i < ctx->num_clients; i++) {
            if (ctx->clients[i]) {
                uvbus_disconnect(ctx->clients[i]);
                uvbus_free(ctx->clients[i]);
            }
        }
        free(ctx->clients);
    }
    
    if (ctx->server) {
        uvbus_stop(ctx->server);
        uvbus_free(ctx->server);
    }
    
    if (ctx->latencies) {
        free(ctx->latencies);
    }
}

/* Performance test 1: Small payload throughput */
void perf_test_small_payload(void) {
    INFO("Running small payload throughput test...");
    
    perf_context_t ctx = {0};
    uv_loop_init(&ctx.loop);
    
    ctx.params.num_requests = 10000;
    ctx.params.payload_size = 64;
    ctx.params.num_clients = 1;
    ctx.params.warmup_requests = TEST_WARMUP_REQUESTS;
    
    ctx.latencies = (double*)malloc(ctx.params.num_requests * sizeof(double));
    if (!ctx.latencies) {
        ERROR("Failed to allocate latencies array");
        return;
    }
    
    if (setup_server(&ctx) < 0) {
        ERROR("Failed to setup server");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    if (setup_clients(&ctx) < 0) {
        ERROR("Failed to setup clients");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    /* Create small payload */
    uint8_t* payload = (uint8_t*)malloc(ctx.params.payload_size);
    memset(payload, 'A', ctx.params.payload_size);
    
    send_requests(&ctx, payload, ctx.params.payload_size);
    
    free(payload);
    
    if (ctx.complete) {
        calculate_results(&ctx);
        print_results("Small Payload Throughput (64 bytes)", &ctx.results, &ctx.params);
    }
    
    cleanup(&ctx);
    uv_loop_close(&ctx.loop);
}

/* Performance test 2: Medium payload throughput */
void perf_test_medium_payload(void) {
    INFO("Running medium payload throughput test...");
    
    perf_context_t ctx = {0};
    uv_loop_init(&ctx.loop);
    
    ctx.params.num_requests = 5000;
    ctx.params.payload_size = 1024;
    ctx.params.num_clients = 1;
    ctx.params.warmup_requests = TEST_WARMUP_REQUESTS;
    
    ctx.latencies = (double*)malloc(ctx.params.num_requests * sizeof(double));
    if (!ctx.latencies) {
        ERROR("Failed to allocate latencies array");
        return;
    }
    
    if (setup_server(&ctx) < 0) {
        ERROR("Failed to setup server");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    if (setup_clients(&ctx) < 0) {
        ERROR("Failed to setup clients");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    /* Create medium payload */
    uint8_t* payload = (uint8_t*)malloc(ctx.params.payload_size);
    memset(payload, 'B', ctx.params.payload_size);
    
    send_requests(&ctx, payload, ctx.params.payload_size);
    
    free(payload);
    
    if (ctx.complete) {
        calculate_results(&ctx);
        print_results("Medium Payload Throughput (1KB)", &ctx.results, &ctx.params);
    }
    
    cleanup(&ctx);
    uv_loop_close(&ctx.loop);
}

/* Performance test 3: Large payload throughput */
void perf_test_large_payload(void) {
    INFO("Running large payload throughput test...");
    
    perf_context_t ctx = {0};
    uv_loop_init(&ctx.loop);
    
    ctx.params.num_requests = 1000;
    ctx.params.payload_size = 65536; /* 64KB */
    ctx.params.num_clients = 1;
    ctx.params.warmup_requests = 10;
    
    ctx.latencies = (double*)malloc(ctx.params.num_requests * sizeof(double));
    if (!ctx.latencies) {
        ERROR("Failed to allocate latencies array");
        return;
    }
    
    if (setup_server(&ctx) < 0) {
        ERROR("Failed to setup server");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    if (setup_clients(&ctx) < 0) {
        ERROR("Failed to setup clients");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    /* Create large payload */
    uint8_t* payload = (uint8_t*)malloc(ctx.params.payload_size);
    memset(payload, 'C', ctx.params.payload_size);
    
    send_requests(&ctx, payload, ctx.params.payload_size);
    
    free(payload);
    
    if (ctx.complete) {
        calculate_results(&ctx);
        print_results("Large Payload Throughput (64KB)", &ctx.results, &ctx.params);
    }
    
    cleanup(&ctx);
    uv_loop_close(&ctx.loop);
}

/* Performance test 4: Concurrent connections */
void perf_test_concurrent_connections(void) {
    INFO("Running concurrent connections test...");
    
    perf_context_t ctx = {0};
    uv_loop_init(&ctx.loop);
    
    ctx.params.num_requests = 5000;
    ctx.params.payload_size = 256;
    ctx.params.num_clients = 10;
    ctx.params.warmup_requests = TEST_WARMUP_REQUESTS;
    
    ctx.latencies = (double*)malloc(ctx.params.num_requests * sizeof(double));
    if (!ctx.latencies) {
        ERROR("Failed to allocate latencies array");
        return;
    }
    
    if (setup_server(&ctx) < 0) {
        ERROR("Failed to setup server");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    if (setup_clients(&ctx) < 0) {
        ERROR("Failed to setup clients");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    /* Create payload */
    uint8_t* payload = (uint8_t*)malloc(ctx.params.payload_size);
    memset(payload, 'D', ctx.params.payload_size);
    
    send_requests(&ctx, payload, ctx.params.payload_size);
    
    free(payload);
    
    if (ctx.complete) {
        calculate_results(&ctx);
        print_results("Concurrent Connections (10 clients)", &ctx.results, &ctx.params);
    }
    
    cleanup(&ctx);
    uv_loop_close(&ctx.loop);
}

/* Performance test 5: Latency-focused test */
void perf_test_latency(void) {
    INFO("Running latency-focused test...");
    
    perf_context_t ctx = {0};
    uv_loop_init(&ctx.loop);
    
    ctx.params.num_requests = 1000;
    ctx.params.payload_size = 32;
    ctx.params.num_clients = 1;
    ctx.params.warmup_requests = 50;
    
    ctx.latencies = (double*)malloc(ctx.params.num_requests * sizeof(double));
    if (!ctx.latencies) {
        ERROR("Failed to allocate latencies array");
        return;
    }
    
    if (setup_server(&ctx) < 0) {
        ERROR("Failed to setup server");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    if (setup_clients(&ctx) < 0) {
        ERROR("Failed to setup clients");
        cleanup(&ctx);
        uv_loop_close(&ctx.loop);
        return;
    }
    
    /* Create small payload */
    uint8_t* payload = (uint8_t*)malloc(ctx.params.payload_size);
    memset(payload, 'E', ctx.params.payload_size);

    /* Send requests with delay to measure latency accurately */
    gettimeofday(&ctx.start_time, NULL);

    for (int i = 0; i < ctx.params.num_requests; i++) {
        ctx.latencies[i] = get_time_ms();
        uvbus_client_send(ctx.clients[0], payload, ctx.params.payload_size);

        /* Wait for response before sending next */
        int retry = 0;
        while (ctx.request_received <= i && retry < 1000) {
            uv_run(&ctx.loop, UV_RUN_NOWAIT);
            usleep(100);
            retry++;
        }
    }

    gettimeofday(&ctx.end_time, NULL);

    INFO("Received %d/%d responses", ctx.request_received, ctx.params.num_requests);

    free(payload);

    if (ctx.complete) {
        calculate_results(&ctx);
        print_results("Latency Test (Sequential)", &ctx.results, &ctx.params);
    }

    cleanup(&ctx);
    uv_loop_close(&ctx.loop);
}

/* Main test runner */
int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("  UVBus Performance Tests\n");
    printf("========================================\n\n");
    
    /* Run all performance tests */
    perf_test_small_payload();
    perf_test_medium_payload();
    perf_test_large_payload();
    perf_test_concurrent_connections();
    perf_test_latency();
    
    printf("\n========================================\n");
    printf("  Performance Tests Complete\n");
    printf("========================================\n");
    
    return 0;
}