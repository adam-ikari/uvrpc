/**
 * UVRPC Benchmark Client
 * Uses new API with uvrpc_config_t
 * Single-threaded event loop model - no locks needed
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#define DEFAULT_SERVER_ADDR "tcp://127.0.0.1:6002"
#define DEFAULT_NUM_REQUESTS 100
#define DEFAULT_CONCURRENCY 10
#define DEFAULT_PAYLOAD_SIZE 128
#define WARMUP_REQUESTS 50
#define DEFAULT_TIMEOUT_MS 30000

/* 延迟统计 */
typedef struct {
    double* latencies;
    int count;
    int capacity;
    double sum;
} latency_stats_t;

/* Per-request context for callback mode */
typedef struct {
    volatile int* completed;
    volatile int* succeeded;
    struct timespec req_start;
    latency_stats_t* stats;
} callback_ctx_t;

/* 测试结果 */
typedef struct {
    double total_time_ms;
    double throughput_ops_per_sec;
    double avg_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
    int success_count;
    int failed_count;
} test_result_t;

static void latency_stats_init(latency_stats_t* stats, int capacity) {
    stats->latencies = (double*)malloc(capacity * sizeof(double));
    stats->count = 0;
    stats->capacity = capacity;
    stats->sum = 0.0;
}

static void latency_stats_record(latency_stats_t* stats, double latency_ms) {
    if (stats->count < stats->capacity) {
        stats->latencies[stats->count] = latency_ms;
        stats->sum += latency_ms;
        stats->count++;
    }
}

/* 回调处理器 - 测量实际延迟 */
static void benchmark_callback(void* ctx, int status,
                                const uint8_t* response_data,
                                size_t response_size) {
    callback_ctx_t* cb_ctx = (callback_ctx_t*)ctx;
    (void)response_data;
    (void)response_size;

    struct timespec req_end;
    clock_gettime(CLOCK_MONOTONIC, &req_end);

    double latency_ms = (req_end.tv_sec - cb_ctx->req_start.tv_sec) * 1000.0 +
                       (req_end.tv_nsec - cb_ctx->req_start.tv_nsec) / 1000000.0;

    if (status == UVRPC_OK) {
        (*cb_ctx->succeeded)++;
        latency_stats_record(cb_ctx->stats, latency_ms);
    }
    (*cb_ctx->completed)++;
}

/* 预热阶段 */
static void warmup(uvrpc_client_t* client, uv_loop_t* loop, uint8_t* test_data, int payload_size) {
    printf("预热阶段 (%d 请求)...\n", WARMUP_REQUESTS);

    for (int i = 0; i < WARMUP_REQUESTS; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, NULL, NULL);
    }

    /* 运行事件循环处理预热请求 */
    for (int i = 0; i < WARMUP_REQUESTS * 2; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("预热完成\n\n");
}

/* 测试1: 串行 Await */
static test_result_t test_serial_await(uvrpc_client_t* client, uv_loop_t* loop, int num_requests,
                                        uint8_t* test_data, int payload_size) {
    printf("[测试1] 串行 Await 模式\n");

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int success_count = 0;
    for (int i = 0; i < num_requests; i++) {
        /* 创建async用于await模式 */
        uvrpc_async_t* async = uvrpc_async_create(loop);
        if (!async) {
            continue;
        }

        struct timespec req_start;
        clock_gettime(CLOCK_MONOTONIC, &req_start);

        /* 使用async API发送请求 */
        uvrpc_client_call_async(client, "echo", "echo", test_data, payload_size, async);

        /* 等待响应 */
        const uvrpc_async_result_t* result = uvrpc_async_await_timeout(async, 5000);
        if (result && result->status == UVRPC_OK) {
            success_count++;

            struct timespec req_end;
            clock_gettime(CLOCK_MONOTONIC, &req_end);

            double latency_ms = (req_end.tv_sec - req_start.tv_sec) * 1000.0 +
                               (req_end.tv_nsec - req_start.tv_nsec) / 1000000.0;
            latency_stats_record(&stats, latency_ms);
        }

        uvrpc_async_free(async);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    test_result_t result;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = 0;
    result.max_latency_ms = 0;
    result.success_count = success_count;
    result.failed_count = num_requests - success_count;

    printf("  总耗时:       %.2f ms\n", elapsed_ms);
    printf("  吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("  成功/失败:    %d/%d\n", success_count, result.failed_count);
    printf("  平均延迟:     %.3f ms\n", result.avg_latency_ms);
    printf("========================================\n");

    free(stats.latencies);
    return result;
}

/* 测试2: 回调模式 */
static test_result_t test_callback(uvrpc_client_t* client, uv_loop_t* loop, int num_requests,
                                    uint8_t* test_data, int payload_size) {
    printf("[测试2] 回调模式\n");

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    volatile int completed = 0;
    volatile int succeeded = 0;
    callback_ctx_t* cb_contexts = (callback_ctx_t*)malloc(num_requests * sizeof(callback_ctx_t));

    for (int i = 0; i < num_requests; i++) {
        cb_contexts[i].completed = &completed;
        cb_contexts[i].succeeded = &succeeded;
        cb_contexts[i].stats = &stats;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 发送所有请求 */
    for (int i = 0; i < num_requests; i++) {
        clock_gettime(CLOCK_MONOTONIC, &cb_contexts[i].req_start);
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size,
                          benchmark_callback, &cb_contexts[i]);
    }

    /* 等待所有响应 - 带超时保护 */
    struct timespec timeout_end;
    timeout_end.tv_sec = start.tv_sec + DEFAULT_TIMEOUT_MS / 1000;
    timeout_end.tv_nsec = start.tv_nsec + (DEFAULT_TIMEOUT_MS % 1000) * 1000000;
    if (timeout_end.tv_nsec >= 1000000000) {
        timeout_end.tv_sec++;
        timeout_end.tv_nsec -= 1000000000;
    }

    while (completed < num_requests) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > timeout_end.tv_sec ||
            (now.tv_sec == timeout_end.tv_sec && now.tv_nsec > timeout_end.tv_nsec)) {
            printf("  超时等待响应 (%d/%d 完成)\n", completed, num_requests);
            break;
        }
        uv_run(loop, UV_RUN_ONCE);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    test_result_t result;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = 0;
    result.max_latency_ms = 0;
    result.success_count = succeeded;
    result.failed_count = num_requests - succeeded;

    printf("  总耗时:       %.2f ms\n", elapsed_ms);
    printf("  吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("  成功/失败:    %d/%d\n", result.success_count, result.failed_count);
    printf("  平均延迟:     %.3f ms\n", result.avg_latency_ms);
    printf("========================================\n");

    free(cb_contexts);
    free(stats.latencies);
    return result;
}

/* 测试3: Async/Await 模式 */
static test_result_t test_async_await(uvrpc_client_t* client, uv_loop_t* loop, int num_requests,
                                       int concurrency, uint8_t* test_data, int payload_size) {
    printf("[测试3] Async/Await 模式 (并发数: %d)\n", concurrency);

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int batch_size = concurrency;
    int batches = (num_requests + batch_size - 1) / batch_size;

    int succeeded = 0;

    for (int batch = 0; batch < batches; batch++) {
        int current_batch_size = (batch == batches - 1) ?
            (num_requests - batch * batch_size) : batch_size;

        /* 创建async数组 */
        uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(current_batch_size * sizeof(uvrpc_async_t*));
        struct timespec* req_starts = (struct timespec*)malloc(current_batch_size * sizeof(struct timespec));

        for (int i = 0; i < current_batch_size; i++) {
            asyncs[i] = uvrpc_async_create(loop);
        }

        /* 使用正确的API发送批量请求 */
        for (int i = 0; i < current_batch_size; i++) {
            clock_gettime(CLOCK_MONOTONIC, &req_starts[i]);
            uvrpc_client_call_async(client, "echo", "echo", test_data, payload_size, asyncs[i]);
        }

        /* 等待所有async完成 */
        for (int i = 0; i < current_batch_size; i++) {
            const uvrpc_async_result_t* result = uvrpc_async_await_timeout(asyncs[i], 5000);
            if (result && result->status == UVRPC_OK) {
                succeeded++;

                struct timespec req_end;
                clock_gettime(CLOCK_MONOTONIC, &req_end);

                double latency_ms = (req_end.tv_sec - req_starts[i].tv_sec) * 1000.0 +
                                   (req_end.tv_nsec - req_starts[i].tv_nsec) / 1000000.0;
                latency_stats_record(&stats, latency_ms);
            }
            uvrpc_async_free(asyncs[i]);
        }

        free(asyncs);
        free(req_starts);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    test_result_t result;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = 0;
    result.max_latency_ms = 0;
    result.success_count = succeeded;
    result.failed_count = num_requests - succeeded;

    printf("  总耗时:       %.2f ms\n", elapsed_ms);
    printf("  吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("  成功/失败:    %d/%d\n", result.success_count, result.failed_count);
    printf("  平均延迟:     %.3f ms\n", result.avg_latency_ms);
    printf("========================================\n");

    free(stats.latencies);
    return result;
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : DEFAULT_SERVER_ADDR;
    int num_requests = (argc > 2) ? atoi(argv[2]) : DEFAULT_NUM_REQUESTS;
    int concurrency = (argc > 3) ? atoi(argv[3]) : DEFAULT_CONCURRENCY;
    int payload_size = (argc > 4) ? atoi(argv[4]) : DEFAULT_PAYLOAD_SIZE;

    printf("========================================\n");
    printf("  UVRPC Benchmark Client\n");
    printf("========================================\n");
    printf("服务器地址:    %s\n", server_addr);
    printf("总请求数:      %d\n", num_requests);
    printf("并发数:        %d\n", concurrency);
    printf("负载大小:      %d bytes\n", payload_size);
    printf("========================================\n\n");

    /* 创建事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建客户端配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* 连接服务器 */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    printf("已连接到服务器\n\n");

    /* 准备测试数据 */
    uint8_t* test_data = (uint8_t*)malloc(payload_size);
    memset(test_data, 'A', payload_size);

    /* 运行事件循环让连接建立 */
    for (int i = 0; i < 10; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* 预热 */
    warmup(client, loop, test_data, payload_size);

    /* 运行测试 */
    test_result_t result1 = test_serial_await(client, loop, num_requests, test_data, payload_size);
    printf("\n");
    test_result_t result2 = test_callback(client, loop, num_requests, test_data, payload_size);
    printf("\n");
    test_result_t result3 = test_async_await(client, loop, num_requests, concurrency, test_data, payload_size);

    /* 清理 */
    free(test_data);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    /* 总结 */
    printf("\n========================================\n");
    printf("  性能对比总结\n");
    printf("========================================\n");
    printf("串行 Await:   %8.2f ops/s (延迟: %.3f ms)\n", result1.throughput_ops_per_sec, result1.avg_latency_ms);
    printf("回调模式:     %8.2f ops/s (延迟: %.3f ms) (%.1fx)\n",
           result2.throughput_ops_per_sec, result2.avg_latency_ms,
           result2.throughput_ops_per_sec / result1.throughput_ops_per_sec);
    printf("Async/Await:  %8.2f ops/s (延迟: %.3f ms) (%.1fx)\n",
           result3.throughput_ops_per_sec, result3.avg_latency_ms,
           result3.throughput_ops_per_sec / result1.throughput_ops_per_sec);
    printf("========================================\n");

    return 0;
}