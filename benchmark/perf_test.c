/**
 * UVRPC Performance Testing Framework
 * Uses new API with uvrpc_config_t
 * Single-threaded event loop model - no locks needed
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define MAX_LATENCY_SAMPLES 100000
#define WARMUP_REQUESTS 100

/* Per-request context for latency tracking */
typedef struct {
    struct timespec req_start;
    int index;
} request_context_t;

/* 延迟统计结构 */
typedef struct {
    double* latencies;
    int count;
    int capacity;
    double min;
    double max;
    double sum;
} latency_stats_t;

/* 测试结果结构 */
typedef struct {
    int total_requests;
    int successful_requests;
    int failed_requests;
    double total_time_ms;
    double throughput_ops_per_sec;
    double avg_latency_ms;
    double p50_latency_ms;
    double p95_latency_ms;
    double p99_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
} test_result_t;

/* 初始化延迟统计 */
static void latency_stats_init(latency_stats_t* stats, int capacity) {
    stats->latencies = (double*)malloc(capacity * sizeof(double));
    stats->count = 0;
    stats->capacity = capacity;
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->sum = 0.0;
}

/* 记录延迟样本 */
static void latency_stats_record(latency_stats_t* stats, double latency_ms) {
    if (stats->count < stats->capacity) {
        stats->latencies[stats->count] = latency_ms;
        stats->sum += latency_ms;
        if (latency_ms < stats->min) stats->min = latency_ms;
        if (latency_ms > stats->max) stats->max = latency_ms;
        stats->count++;
    }
}

/* 计算百分位数 */
static double calculate_percentile(double* data, int count, double percentile) {
    if (count == 0) return 0.0;

    /* 简单的冒泡排序 */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (data[j] > data[j + 1]) {
                double temp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }

    /* 计算百分位数 */
    int index = (int)ceil((percentile / 100.0) * count) - 1;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;

    return data[index];
}

/* 打印测试结果 */
static void print_test_results(const char* test_name, const test_result_t* result) {
    printf("\n========== %s 结果 ==========\n", test_name);
    printf("总请求数:     %d\n", result->total_requests);
    printf("成功:         %d (%.2f%%)\n", result->successful_requests,
           (result->successful_requests * 100.0) / result->total_requests);
    printf("失败:         %d (%.2f%%)\n", result->failed_requests,
           (result->failed_requests * 100.0) / result->total_requests);
    printf("总耗时:       %.2f ms\n", result->total_time_ms);
    printf("吞吐量:       %.2f ops/s\n", result->throughput_ops_per_sec);
    printf("----------------------------------\n");
    printf("延迟统计:\n");
    printf("  平均:       %.3f ms\n", result->avg_latency_ms);
    printf("  最小:       %.3f ms\n", result->min_latency_ms);
    printf("  最大:       %.3f ms\n", result->max_latency_ms);
    printf("  P50:        %.3f ms\n", result->p50_latency_ms);
    printf("  P95:        %.3f ms\n", result->p95_latency_ms);
    printf("  P99:        %.3f ms\n", result->p99_latency_ms);
    printf("=========================================\n");
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

/* 测试1: 回调模式 - 正确的计时和追踪 */
static test_result_t test_callback(uvrpc_client_t* client, uv_loop_t* loop, int num_requests,
                                   uint8_t* test_data, int payload_size) {
    printf("[测试1] 回调模式\n");

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    volatile int completed = 0;
    volatile int succeeded = 0;
    request_context_t* req_contexts = (request_context_t*)malloc(num_requests * sizeof(request_context_t));

    void callback(void* ctx, int status,
                  const uint8_t* response_data,
                  size_t response_size) {
        request_context_t* req_ctx = (request_context_t*)ctx;
        (void)response_data;
        (void)response_size;

        struct timespec req_end;
        clock_gettime(CLOCK_MONOTONIC, &req_end);

        double latency_ms = (req_end.tv_sec - req_ctx->req_start.tv_sec) * 1000.0 +
                           (req_end.tv_nsec - req_ctx->req_start.tv_nsec) / 1000000.0;

        if (status == UVRPC_OK) {
            succeeded++;
            latency_stats_record(&stats, latency_ms);
        }
        completed++;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 发送所有请求，在计时窗口开始之前 */
    for (int i = 0; i < num_requests; i++) {
        clock_gettime(CLOCK_MONOTONIC, &req_contexts[i].req_start);
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size,
                          callback, &req_contexts[i]);
    }

    /* 等待所有响应 - 使用超时保护 */
    int timeout_ms = 30000;  // 30秒超时
    struct timespec timeout_end;
    timeout_end.tv_sec = start.tv_sec + timeout_ms / 1000;
    timeout_end.tv_nsec = start.tv_nsec + (timeout_ms % 1000) * 1000000;
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
    result.total_requests = num_requests;
    result.successful_requests = succeeded;
    result.failed_requests = num_requests - succeeded;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = stats.min;
    result.max_latency_ms = stats.max;
    result.p50_latency_ms = calculate_percentile(stats.latencies, stats.count, 50);
    result.p95_latency_ms = calculate_percentile(stats.latencies, stats.count, 95);
    result.p99_latency_ms = calculate_percentile(stats.latencies, stats.count, 99);

    print_test_results("回调模式", &result);

    free(req_contexts);
    free(stats.latencies);
    return result;
}

/* 测试2: Async/Await 模式 - 正确使用 API */
static test_result_t test_async_await(uvrpc_client_t* client, uv_loop_t* loop, int num_requests,
                                      int concurrency, uint8_t* test_data, int payload_size) {
    printf("[测试2] Async/Await 模式 (并发数: %d)\n", concurrency);

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    int batch_size = concurrency;
    int batches = (num_requests + batch_size - 1) / batch_size;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int completed = 0;
    int succeeded = 0;

    for (int batch = 0; batch < batches; batch++) {
        int current_batch_size = (batch == batches - 1) ?
            (num_requests - batch * batch_size) : batch_size;

        /* 创建async数组 */
        uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(current_batch_size * sizeof(uvrpc_async_t*));
        request_context_t* req_contexts = (request_context_t*)malloc(current_batch_size * sizeof(request_context_t));

        for (int i = 0; i < current_batch_size; i++) {
            asyncs[i] = uvrpc_async_create(loop);
            req_contexts[i].index = batch * batch_size + i;
        }

        /* 使用正确的API：uvrpc_client_call_async */
        for (int i = 0; i < current_batch_size; i++) {
            clock_gettime(CLOCK_MONOTONIC, &req_contexts[i].req_start);
            uvrpc_client_call_async(client, "echo", "echo", test_data, payload_size, asyncs[i]);
        }

        /* 等待所有async完成 */
        for (int i = 0; i < current_batch_size; i++) {
            const uvrpc_async_result_t* result = uvrpc_async_await(asyncs[i]);
            if (result && result->status == UVRPC_OK) {
                succeeded++;

                struct timespec req_end;
                clock_gettime(CLOCK_MONOTONIC, &req_end);

                double latency_ms = (req_end.tv_sec - req_contexts[i].req_start.tv_sec) * 1000.0 +
                                   (req_end.tv_nsec - req_contexts[i].req_start.tv_nsec) / 1000000.0;
                latency_stats_record(&stats, latency_ms);
            }
            completed++;
            uvrpc_async_free(asyncs[i]);
        }

        free(asyncs);
        free(req_contexts);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    test_result_t result;
    result.total_requests = num_requests;
    result.successful_requests = succeeded;
    result.failed_requests = num_requests - succeeded;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = stats.min;
    result.max_latency_ms = stats.max;
    result.p50_latency_ms = calculate_percentile(stats.latencies, stats.count, 50);
    result.p95_latency_ms = calculate_percentile(stats.latencies, stats.count, 95);
    result.p99_latency_ms = calculate_percentile(stats.latencies, stats.count, 99);

    print_test_results("Async/Await", &result);

    free(stats.latencies);
    return result;
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:6002";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 1000;
    int concurrency = (argc > 3) ? atoi(argv[3]) : 10;
    int payload_size = (argc > 4) ? atoi(argv[4]) : 128;

    printf("========================================\n");
    printf("  UVRPC 综合性能测试框架\n");
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
    test_result_t result1 = test_callback(client, loop, num_requests, test_data, payload_size);
    printf("\n");
    test_result_t result2 = test_async_await(client, loop, num_requests, concurrency, test_data, payload_size);

    /* 清理 */
    free(test_data);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    /* 总结 */
    printf("\n========================================\n");
    printf("  性能对比总结\n");
    printf("========================================\n");
    printf("回调模式:     %8.2f ops/s, P99: %6.3f ms\n",
           result1.throughput_ops_per_sec, result1.p99_latency_ms);
    printf("Async/Await:  %8.2f ops/s (%.1fx 提升)\n",
           result2.throughput_ops_per_sec,
           result2.throughput_ops_per_sec / result1.throughput_ops_per_sec);
    printf("========================================\n");

    return 0;
}