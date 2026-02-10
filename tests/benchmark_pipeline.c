#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 流水线测试 - 多个请求并行 */
void benchmark_pipeline(const char* server_addr, int num_requests, int pipeline_size) {
    printf("\n========== 流水线性能测试 ==========\n");
    printf("服务器地址: %s\n", server_addr);
    printf("总请求数: %d\n", num_requests);
    printf("流水线大小: %d\n", pipeline_size);
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
    
    volatile int sent = 0;
    volatile int completed = 0;
    volatile int success_count = 0;
    
    /* 响应回调 */
    void response_callback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
        (void)ctx;
        (void)response_data;
        (void)response_size;
        
        completed++;
        if (status == UVRPC_OK) {
            success_count++;
        }
        
        /* 发送下一个请求（保持流水线满载） */
        if (sent < num_requests) {
            uint8_t data[128];
            memset(data, 'A', sizeof(data));
            
            if (uvrpc_client_call(client, "echo", "test", 
                                   data, sizeof(data),
                                   response_callback, NULL) == UVRPC_OK) {
                sent++;
            }
        }
    }
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* 启动流水线 - 发送初始 batch */
    for (int i = 0; i < pipeline_size && i < num_requests; i++) {
        uint8_t data[128];
        memset(data, 'A', sizeof(data));
        
        if (uvrpc_client_call(client, "echo", "test", 
                               data, sizeof(data),
                               response_callback, NULL) == UVRPC_OK) {
            sent++;
        }
    }
    
    /* 运行事件循环直到所有请求完成 */
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
    int pipeline_size = (argc > 3) ? atoi(argv[3]) : 10;
    
    benchmark_pipeline(server_addr, num_requests, pipeline_size);
    
    return 0;
}