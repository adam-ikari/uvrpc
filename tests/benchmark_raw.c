#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 原始数据基准测试 - 不使用序列化 */
void benchmark_raw_data(const char* server_addr, int num_requests) {
    printf("\n========== 原始数据基准测试 ==========\n");
    printf("服务器地址: %s\n", server_addr);
    printf("请求数量: %d\n", num_requests);
    printf("======================================\n\n");
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect\n");
        uvrpc_client_free(client);
        return;
    }
    
    /* 准备原始数据（不序列化） */
    uint8_t raw_data[128];
    memset(raw_data, 'A', sizeof(raw_data));
    
    volatile int completed = 0;
    volatile int success_count = 0;
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* 发送所有请求 */
    for (int i = 0; i < num_requests; i++) {
        if (uvrpc_client_call(client, "echo", "raw", 
                               raw_data, sizeof(raw_data),
                               NULL, NULL) != UVRPC_OK) {
            fprintf(stderr, "Failed to send request %d\n", i);
        }
        
        /* 运行事件循环处理响应 */
        uv_run(loop, UV_RUN_NOWAIT);
    }
    
    /* 等待所有响应 */
    while (completed < num_requests) {
        uv_run(loop, UV_RUN_ONCE);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    
    printf("\n========== 测试结果 ==========\n");
    printf("总请求数: %d\n", num_requests);
    printf("成功: %d\n", success_count);
    printf("总耗时: %.2f ms\n", total_time_ms);
    printf("吞吐量: %.2f ops/s\n", (success_count * 1000.0) / total_time_ms);
    printf("平均延迟: %.3f ms\n", total_time_ms / num_requests);
    printf("==============================\n\n");
    
    uvrpc_client_free(client);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 10000;
    
    benchmark_raw_data(server_addr, num_requests);
    
    return 0;
}