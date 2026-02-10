#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CLIENTS 10

/* 多客户端并发测试 */
void benchmark_multi_client(const char* server_addr, int num_requests, int num_clients) {
    printf("\n========== 多客户端并发测试 ==========\n");
    printf("服务器地址: %s\n", server_addr);
    printf("总请求数: %d\n", num_requests);
    printf("客户端数量: %d\n", num_clients);
    printf("每客户端请求数: %d\n", num_requests / num_clients);
    printf("=========================================\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* clients[MAX_CLIENTS];
    volatile int requests_per_client = num_requests / num_clients;
    volatile int completed[MAX_CLIENTS] = {0};
    volatile int success_count[MAX_CLIENTS] = {0};
    
    /* 创建多个客户端 */
    for (int i = 0; i < num_clients; i++) {
        clients[i] = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
        if (!clients[i]) {
            fprintf(stderr, "Failed to create client %d\n", i);
            continue;
        }
        
        if (uvrpc_client_connect(clients[i]) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect client %d\n", i);
            continue;
        }
    }
    
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
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* 所有客户端并发发送请求 */
    int client_ids[MAX_CLIENTS];
    for (int i = 0; i < num_clients; i++) {
        client_ids[i] = i;
        
        for (int j = 0; j < requests_per_client; j++) {
            uint8_t data[128];
            memset(data, 'A', sizeof(data));
            
            if (uvrpc_client_call(clients[i], "echo.EchoService", "echo", 
                                   data, sizeof(data),
                                   response_callback, &client_ids[i]) != UVRPC_OK) {
                fprintf(stderr, "Failed to send request client=%d req=%d\n", i, j);
            }
        }
    }
    
    /* 运行事件循环直到所有请求完成 */
    int total_completed = 0;
    while (total_completed < num_requests) {
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
    printf("总请求数: %d\n", num_requests);
    printf("成功: %d\n", total_success);
    printf("总耗时: %.2f ms\n", total_time_ms);
    printf("吞吐量: %.2f ops/s\n", (total_success * 1000.0) / total_time_ms);
    printf("平均延迟: %.3f ms\n", total_time_ms / num_requests);
    printf("==============================\n\n");
    
    /* 清理 */
    for (int i = 0; i < num_clients; i++) {
        if (clients[i]) {
            uvrpc_client_free(clients[i]);
        }
    }
    
    uv_loop_close(&loop);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    int num_requests = (argc > 2) ? atoi(argv[2]) : 10000;
    int num_clients = (argc > 3) ? atoi(argv[3]) : 5;
    
    if (num_clients > MAX_CLIENTS) {
        num_clients = MAX_CLIENTS;
    }
    
    benchmark_multi_client(server_addr, num_requests, num_clients);
    
    return 0;
}