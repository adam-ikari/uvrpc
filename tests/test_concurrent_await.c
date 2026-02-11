#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/* 延迟服务处理器 - 模拟慢速服务 */
int delayed_handler(void* ctx,
                    const uint8_t* request_data,
                    size_t request_size,
                    uint8_t** response_data,
                    size_t* response_size) {
    (void)ctx;
    (void)request_data;
    (void)request_size;
    
    /* 模拟处理延迟（50-100ms） */
    int delay_ms = 50 + (rand() % 50);
    usleep(delay_ms * 1000);
    
    /* 返回延迟时间 */
    *response_data = (uint8_t*)UVRPC_MALLOC(sizeof(int));
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memcpy(*response_data, &delay_ms, sizeof(int));
    *response_size = sizeof(int);
    
    return UVRPC_OK;
}

/* 服务器线程函数 */
void* server_thread(void* arg) {
    (void)arg;
    const char* server_addr = "tcp://127.0.0.1:5558";
    
    printf("[SERVER] Starting on %s...\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 使用 ROUTER 模式支持多客户端并发 */
    uvrpc_server_t* server = uvrpc_server_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!server) {
        fprintf(stderr, "[SERVER] Failed to create server\n");
        return NULL;
    }
    
    uvrpc_server_register_service(server, "delayed.DelayedService", delayed_handler, NULL);
    
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[SERVER] Failed to start server\n");
        uvrpc_server_free(server);
        return NULL;
    }
    
    printf("[SERVER] Running...\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    
    printf("[SERVER] Stopped\n");
    return NULL;
}

/* 测试并发 await */
int client_test_concurrent() {
    const char* server_addr = "tcp://127.0.0.1:5558";
    
    printf("[CLIENT] Connecting to %s...\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 使用 DEALER 模式支持并发请求 */
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!client) {
        fprintf(stderr, "[CLIENT] Failed to create client\n");
        return 1;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("[CLIENT] Connected\n");
    
    const int num_requests = 5;
    uvrpc_async_t* asyncs[num_requests];
    uvrpc_async_result_t results[num_requests];
    
    /* 准备请求数据 */
    const char* services[num_requests];
    const char* methods[num_requests];
    uint8_t* req_data[num_requests];
    size_t req_size[num_requests];
    
    for (int i = 0; i < num_requests; i++) {
        services[i] = "delayed.DelayedService";
        methods[i] = "delayed";
        
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = 100 + i;
        req.service_id = (char*)services[i];
        req.method_id = (char*)methods[i];
        req.request_data = (uint8_t*)&i;
        req.request_data_size = sizeof(int);
        
        if (uvrpc_serialize_request_msgpack(&req, &req_data[i], &req_size[i]) != 0) {
            fprintf(stderr, "[CLIENT] Failed to serialize request %d\n", i);
            for (int j = 0; j < i; j++) {
                uvrpc_free_serialized_data(req_data[j]);
            }
            uvrpc_client_free(client);
            return 1;
        }
    }
    
    printf("\n[CLIENT] Test: Concurrent await (all requests)\n");
    printf("[CLIENT] Starting %d concurrent requests...\n", num_requests);
    
    /* 记录开始时间 */
    clock_t start = clock();
    
    /* 使用并发 await 宏 */
    UVRPC_AWAIT_ALL(results, asyncs, num_requests, client, services, methods, req_data, req_size);
    
    /* 记录结束时间 */
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC * 1000;
    
    printf("[CLIENT] All %d requests completed in %.2f ms\n", num_requests, elapsed);
    printf("[CLIENT] Average time per request: %.2f ms\n", elapsed / num_requests);
    
    /* 验证结果 */
    int success_count = 0;
    for (int i = 0; i < num_requests; i++) {
        if (results[i].status == UVRPC_OK) {
            int delay_ms;
            memcpy(&delay_ms, results[i].response_data, sizeof(int));
            printf("  [CLIENT] Request #%d: ✓ Success (server delay: %d ms)\n", i, delay_ms);
            success_count++;
        } else {
            printf("  [CLIENT] Request #%d: ✗ Failed (%s)\n", i, uvrpc_strerror(results[i].status));
        }
    }
    
    /* 释放资源 */
    for (int i = 0; i < num_requests; i++) {
        uvrpc_async_free(asyncs[i]);
        uvrpc_free_serialized_data(req_data[i]);
    }
    
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    return (success_count == num_requests) ? 0 : 1;
}

/* 测试串行 await（用于对比） */
int client_test_serial() {
    const char* server_addr = "tcp://127.0.0.1:5558";
    
    printf("\n[CLIENT] Test: Serial await (for comparison)\n");
    printf("[CLIENT] Connecting to %s...\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 使用 DEALER 模式支持并发请求 */
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!client) {
        fprintf(stderr, "[CLIENT] Failed to create client\n");
        return 1;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("[CLIENT] Connected\n");
    
    const int num_requests = 5;
    uvrpc_async_t* async = uvrpc_async_new(&loop);
    if (!async) {
        fprintf(stderr, "[CLIENT] Failed to create async context\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    /* 记录开始时间 */
    clock_t start = clock();
    
    int success_count = 0;
    for (int i = 0; i < num_requests; i++) {
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = 200 + i;
        req.service_id = "delayed.DelayedService";
        req.method_id = "delayed";
        req.request_data = (uint8_t*)&i;
        req.request_data_size = sizeof(int);
        
        uint8_t* serialized = NULL;
        size_t size = 0;
        if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) != 0) {
            fprintf(stderr, "[CLIENT] Failed to serialize request %d\n", i);
            continue;
        }
        
        uvrpc_async_result_t result;
        UVRPC_AWAIT(result, async, client, "delayed.DelayedService", "delayed", serialized, size);
        
        if (result.status == UVRPC_OK) {
            int delay_ms;
            memcpy(&delay_ms, result.response_data, sizeof(int));
            printf("  [CLIENT] Request #%d: ✓ Success (server delay: %d ms)\n", i, delay_ms);
            success_count++;
        } else {
            printf("  [CLIENT] Request #%d: ✗ Failed (%s)\n", i, uvrpc_strerror(result.status));
        }
        
        uvrpc_free_serialized_data(serialized);
    }
    
    /* 记录结束时间 */
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC * 1000;
    
    printf("[CLIENT] All %d requests completed in %.2f ms (serial)\n", num_requests, elapsed);
    printf("[CLIENT] Average time per request: %.2f ms\n", elapsed / num_requests);
    
    /* 释放资源 */
    uvrpc_async_free(async);
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    return (success_count == num_requests) ? 0 : 1;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  Concurrent Await Test\n");
    printf("========================================\n\n");
    
    srand(time(NULL));
    
    /* 启动服务器线程 */
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, server_thread, NULL);
    
    /* 等待服务器启动 */
    sleep(1);
    
    /* 运行测试 */
    int result1 = client_test_concurrent();
    int result2 = client_test_serial();
    
    /* 测试结果 */
    printf("\n========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("Concurrent await: %s\n", result1 == 0 ? "PASSED ✓" : "FAILED ✗");
    printf("Serial await: %s\n", result2 == 0 ? "PASSED ✓" : "FAILED ✗");
    printf("========================================\n");
    
    if (result1 == 0 && result2 == 0) {
        printf("  ALL TESTS PASSED ✓\n");
        printf("\nNote: Concurrent await should be ~3-5x faster than serial\n");
        printf("      because requests are sent and processed in parallel.\n");
    } else {
        printf("  SOME TESTS FAILED ✗\n");
    }
    printf("========================================\n");
    
    return (result1 == 0 && result2 == 0) ? 0 : 1;
}