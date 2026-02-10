#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 高性能客户端 - 并发请求 */
int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:6000";
    int num_clients = (argc > 2) ? atoi(argv[2]) : 10;
    int requests_per_client = (argc > 3) ? atoi(argv[3]) : 1000;
    int payload_size = (argc > 4) ? atoi(argv[4]) : 128;
    
    printf("========================================\n");
    printf("高性能 RPC 客户端 (DEALER 模式)\n");
    printf("========================================\n");
    printf("服务器地址: %s\n", server_addr);
    printf("客户端数量: %d\n", num_clients);
    printf("每客户端请求数: %d\n", requests_per_client);
    printf("总请求数: %d\n", num_clients * requests_per_client);
    printf("负载大小: %d bytes\n", payload_size);
    printf("========================================\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建多个 DEALER 客户端 */
    uvrpc_client_t** clients = (uvrpc_client_t**)malloc(num_clients * sizeof(uvrpc_client_t*));
    if (!clients) {
        fprintf(stderr, "Failed to allocate clients array\n");
        return 1;
    }
    
    int* client_ids = (int*)malloc(num_clients * sizeof(int));
    int* completed = (int*)calloc(num_clients, sizeof(int));
    int* success_count = (int*)calloc(num_clients, sizeof(int));
    
    /* 准备负载数据 */
    uint8_t* payload = (uint8_t*)malloc(payload_size);
    memset(payload, 'A', payload_size);
    
    /* 创建并连接所有客户端 */
    for (int i = 0; i < num_clients; i++) {
        client_ids[i] = i;
        clients[i] = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
        if (!clients[i]) {
            fprintf(stderr, "Failed to create client %d\n", i);
            continue;
        }
        
        if (uvrpc_client_connect(clients[i]) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect client %d\n", i);
        }
    }
    
    printf("预热中...\n");
    uv_run(&loop, UV_RUN_ONCE);
    uv_run(&loop, UV_RUN_ONCE);
    printf("预热完成\n\n");
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* 响应回调 */
    void response_callback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
        int client_id = *(int*)ctx;
        (void)response_data;
        (void)response_size;
        
        completed[client_id]++;
        if (status == UVRPC_OK) {
            success_count[client_id]++;
        }
    }
    
    /* 所有客户端并发发送请求 */
    printf("开始发送请求...\n");
    for (int i = 0; i < num_clients; i++) {
        if (!clients[i]) continue;
        
        for (int j = 0; j < requests_per_client; j++) {
            uvrpc_client_call(clients[i], "echo", "perf",
                             payload, payload_size,
                             response_callback, &client_ids[i]);
        }
    }
    
    /* 运行事件循环直到所有请求完成 */
    int total_completed = 0;
    int total_requests = num_clients * requests_per_client;
    
    while (total_completed < total_requests) {
        uv_run(&loop, UV_RUN_ONCE);
        
        total_completed = 0;
        for (int i = 0; i < num_clients; i++) {
            total_completed += completed[i];
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    
    int total_success = 0;
    for (int i = 0; i < num_clients; i++) {
        total_success += success_count[i];
    }
    
    printf("\n========== 测试结果 ==========\n");
    printf("总请求数: %d\n", total_requests);
    printf("成功: %d\n", total_success);
    printf("失败: %d\n", total_requests - total_success);
    printf("总耗时: %.2f ms\n", total_time_ms);
    printf("吞吐量: %.2f ops/s\n", (total_success * 1000.0) / total_time_ms);
    printf("平均延迟: %.3f ms\n", total_time_ms / total_requests);
    printf("==============================\n\n");
    
    /* 清理 */
    for (int i = 0; i < num_clients; i++) {
        if (clients[i]) {
            uvrpc_client_free(clients[i]);
        }
    }
    
    free(clients);
    free(client_ids);
    free(completed);
    free(success_count);
    free(payload);
    
    uv_loop_close(&loop);
    
    return 0;
}