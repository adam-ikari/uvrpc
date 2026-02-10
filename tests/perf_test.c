#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 性能测试结果 */
typedef struct {
    int total_requests;
    int success_count;
    int error_count;
    double total_time_ms;
    double min_time_ms;
    double max_time_ms;
    double avg_time_ms;
    double throughput_ops_per_sec;
} perf_result_t;

/* 测试上下文 */
typedef struct {
    volatile int completed;
    int num_requests;
    int payload_size;
    struct timespec* start_times;
    struct timespec* end_times;
    uvrpc_client_t* client;
    const uint8_t* serialized_data;
    size_t serialized_size;
} test_context_t;

/* 响应回调 */
void response_callback(void* ctx, int status,
                       const uint8_t* response_data,
                       size_t response_size) {
    (void)response_data;
    (void)response_size;
    
    test_context_t* test_ctx = (test_context_t*)ctx;
    int idx = test_ctx->completed;
    
    clock_gettime(CLOCK_MONOTONIC, &test_ctx->end_times[idx]);
    
    if (status == UVRPC_OK) {
        /* 成功 */
    }
    
    /* 继续发送下一个请求 */
    idx++;
    test_ctx->completed = idx;
    
    if (idx < test_ctx->num_requests) {
        clock_gettime(CLOCK_MONOTONIC, &test_ctx->start_times[idx]);
        
        if (uvrpc_client_call(test_ctx->client, "echo.EchoService", "echo",
                               test_ctx->serialized_data, test_ctx->serialized_size,
                               response_callback, ctx) != UVRPC_OK) {
            fprintf(stderr, "Failed to send request %d\n", idx);
        }
    } else {
        /* 所有请求完成 */
        uv_stop(uv_default_loop());
    }
}

/* 性能测试 */
void run_perf_test(const char* server_addr, int num_requests, int payload_size) {
    printf("\n========== 性能测试 ==========\n");
    printf("服务器地址: %s\n", server_addr);
    printf("请求数量: %d\n", num_requests);
    printf("负载大小: %d bytes\n", payload_size);
    printf("==============================\n\n");
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return;
    }
    
    /* 连接到服务器 */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        return;
    }
    
    /* 准备负载数据 */
    uint8_t* payload = (uint8_t*)malloc(payload_size);
    if (!payload) {
        fprintf(stderr, "Failed to allocate payload\n");
        uvrpc_client_free(client);
        return;
    }
    memset(payload, 'A', payload_size);
    
    /* 使用 msgpack_wrapper 序列化请求 */
    uvrpc_request_t request;
    memset(&request, 0, sizeof(request));
    request.request_id = 1;
    request.service_id = "echo.EchoService";
    request.method_id = "echo";
    request.request_data = payload;
    request.request_data_size = payload_size;
    
    uint8_t* serialized_data = NULL;
    size_t serialized_size = 0;
    if (uvrpc_serialize_request_msgpack(&request, &serialized_data, &serialized_size) != 0) {
        fprintf(stderr, "Failed to serialize request\n");
        free(payload);
        uvrpc_client_free(client);
        return;
    }
    
    /* 创建测试上下文 */
    test_context_t test_ctx;
    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.completed = 0;
    test_ctx.num_requests = num_requests;
    test_ctx.payload_size = payload_size;
    test_ctx.client = client;
    test_ctx.serialized_data = serialized_data;
    test_ctx.serialized_size = serialized_size;
    
    test_ctx.start_times = (struct timespec*)malloc(num_requests * sizeof(struct timespec));
    test_ctx.end_times = (struct timespec*)malloc(num_requests * sizeof(struct timespec));
    
    printf("开始性能测试...\n");
    
    /* 发送第一个请求 */
    clock_gettime(CLOCK_MONOTONIC, &test_ctx.start_times[0]);
    if (uvrpc_client_call(client, "echo.EchoService", "echo",
                           test_ctx.serialized_data, test_ctx.serialized_size,
                           response_callback, &test_ctx) != UVRPC_OK) {
        fprintf(stderr, "Failed to send first request\n");
        goto cleanup;
    }
    
    /* 运行事件循环 */
    struct timespec test_start, test_end;
    clock_gettime(CLOCK_MONOTONIC, &test_start);
    
    uv_run(loop, UV_RUN_DEFAULT);
    
    clock_gettime(CLOCK_MONOTONIC, &test_end);
    
    /* 计算结果 */
    perf_result_t result;
    memset(&result, 0, sizeof(result));
    
    double test_time_ms = (test_end.tv_sec - test_start.tv_sec) * 1000.0 +
                          (test_end.tv_nsec - test_start.tv_nsec) / 1000000.0;
    
    result.total_requests = num_requests;
    result.total_time_ms = test_time_ms;
    result.success_count = test_ctx.completed;
    result.error_count = num_requests - test_ctx.completed;
    result.throughput_ops_per_sec = (test_ctx.completed * 1000.0) / test_time_ms;
    
    /* 计算延迟统计 */
    double total_latency = 0;
    result.min_time_ms = 1e9;
    result.max_time_ms = 0;
    
    for (int i = 0; i < test_ctx.completed; i++) {
        double latency_ms = (test_ctx.end_times[i].tv_sec - test_ctx.start_times[i].tv_sec) * 1000.0 +
                           (test_ctx.end_times[i].tv_nsec - test_ctx.start_times[i].tv_nsec) / 1000000.0;
        
        total_latency += latency_ms;
        
        if (latency_ms < result.min_time_ms) {
            result.min_time_ms = latency_ms;
        }
        if (latency_ms > result.max_time_ms) {
            result.max_time_ms = latency_ms;
        }
    }
    
    if (test_ctx.completed > 0) {
        result.avg_time_ms = total_latency / test_ctx.completed;
    }
    
    /* 打印结果 */
    printf("\n========== 测试结果 ==========\n");
    printf("总请求数: %d\n", result.total_requests);
    printf("成功: %d\n", result.success_count);
    printf("失败: %d\n", result.error_count);
    printf("总耗时: %.2f ms\n", result.total_time_ms);
    printf("吞吐量: %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("平均延迟: %.3f ms\n", result.avg_time_ms);
    printf("最小延迟: %.3f ms\n", result.min_time_ms);
    printf("最大延迟: %.3f ms\n", result.max_time_ms);
    printf("==============================\n\n");

cleanup:
    uvrpc_free_serialized_data((uint8_t*)serialized_data);
    free(payload);
    free(test_ctx.start_times);
    free(test_ctx.end_times);
    uvrpc_client_free(client);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 1000;
    int payload_size = (argc > 3) ? atoi(argv[3]) : 128;
    
    printf("uvrpc 性能测试工具\n");
    printf("用法: %s [server_addr] [num_requests] [payload_size]\n", argv[0]);
    printf("默认: %s tcp://127.0.0.1:5555 1000 128\n\n", argv[0]);
    
    run_perf_test(server_addr, num_requests, payload_size);
    
    return 0;
}
