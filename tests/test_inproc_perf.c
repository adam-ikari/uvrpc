#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int echo_handler(void* ctx, const uint8_t* request_data, size_t request_size,
                        uint8_t** response_data, size_t* response_size) {
    (void)ctx;
    *response_data = (uint8_t*)malloc(request_size);
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    return 0;
}

/* 测试内联调用 - 共享loop，直接同步调用 */
static void test_inline_call(uvrpc_client_t* client, uv_loop_t* loop, int num_requests, int payload_size) {
    printf("[测试] Inproc 内联调用（共享loop，直接同步调用）\n");
    printf("  准备发送 %d 个请求...\n", num_requests);
    
    uint8_t* test_data = (uint8_t*)malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    printf("  开始发送请求...\n");
    int success_count = 0;
    for (int i = 0; i < num_requests; i++) {
        uvrpc_response_t response;
        memset(&response, 0, sizeof(response));
        
        /* 内联调用：直接同步调用，手动运行loop */
        volatile int call_completed = 0;
        
        void inline_callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
            (void)ctx;
            if (status == 0) {
                response.status = status;
                response.response_data = (uint8_t*)malloc(response_size);
                memcpy(response.response_data, response_data, response_size);
                response.response_data_size = response_size;
                success_count++;
            }
            call_completed = 1;
        }
        
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, inline_callback, NULL);
        
        /* 运行loop直到响应到达 */
        while (!call_completed) {
            uv_run(loop, UV_RUN_NOWAIT);
        }
        
        /* 释放响应数据 */
        if (response.response_data) {
            free(response.response_data);
        }
        
        if ((i + 1) % 20 == 0) {
            printf("  进度: %d/%d\n", i + 1, num_requests);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    printf("  总请求数: %d\n", num_requests);
    printf("  成功: %d\n", success_count);
    printf("  总耗时: %.3f ms\n", elapsed_ms);
    printf("  吞吐量: %.0f ops/s\n", (num_requests / elapsed_ms) * 1000);
    printf("  平均延迟: %.3f ms\n", elapsed_ms / num_requests);
    printf("\n");
    
    free(test_data);
}

/* 测试回调模式 - 使用独立loop（同一线程） */
static void test_callback_single_thread(uvrpc_client_t* client, uv_loop_t* client_loop, uv_loop_t* server_loop, int num_requests, int payload_size) {
    printf("[测试] Inproc 回调模式（独立loop，同一线程）\n");
    printf("  准备发送 %d 个请求...\n", num_requests);
    
    uint8_t* test_data = (uint8_t*)malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    volatile int completed = 0;
    volatile int succeeded = 0;
    
    void callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
        (void)ctx;
        (void)response_data;
        (void)response_size;
        if (status == 0) succeeded++;
        completed++;
    }
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    printf("  开始发送请求...\n");
    for (int i = 0; i < num_requests; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, callback, NULL);
        /* 手动调度两个loop */
        while (completed <= i && completed < num_requests) {
            uv_run(server_loop, UV_RUN_NOWAIT);
            uv_run(client_loop, UV_RUN_NOWAIT);
        }
        if ((i + 1) % 20 == 0) {
            printf("  进度: %d/%d (已完成: %d)\n", i + 1, num_requests, completed);
        }
    }
    
    /* 等待所有请求完成 */
    int loop_count = 0;
    while (completed < num_requests && loop_count < 500) {
        uv_run(server_loop, UV_RUN_NOWAIT);
        uv_run(client_loop, UV_RUN_NOWAIT);
        loop_count++;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    printf("  总请求数: %d\n", num_requests);
    printf("  成功: %d\n", succeeded);
    printf("  总耗时: %.3f ms\n", elapsed_ms);
    printf("  吞吐量: %.0f ops/s\n", (num_requests / elapsed_ms) * 1000);
    printf("  平均延迟: %.3f ms\n", elapsed_ms / num_requests);
    printf("\n");
    
    free(test_data);
}

/* 测试并发await模式 - 使用共享loop（单线程） */
static void test_concurrent_await_shared_loop(uvrpc_client_t* client, uv_loop_t* loop, int num_requests, int concurrency, int payload_size) {
    printf("[测试] Inproc 并发 Await 模式（共享loop，单线程）\n");
    printf("  准备发送 %d 个请求...\n", num_requests);
    
    uint8_t* test_data = (uint8_t*)malloc(payload_size);
    memset(test_data, 'A', payload_size);
    
    int batch_size = concurrency;
    int batches = (num_requests + batch_size - 1) / batch_size;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int success_count = 0;
    
    printf("  开始测试，共 %d 个批次...\n", batches);
    
    for (int batch = 0; batch < batches; batch++) {
        int current_batch_size = (batch == batches - 1) ? 
            (num_requests - batch * batch_size) : batch_size;
        
        printf("  批次 %d/%d，大小: %d\n", batch + 1, batches, current_batch_size);
        
        uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(current_batch_size * sizeof(uvrpc_async_t*));
        for (int i = 0; i < current_batch_size; i++) {
            asyncs[i] = uvrpc_async_new(loop);
        }
        
        /* 发送请求 */
        for (int i = 0; i < current_batch_size; i++) {
            uvrpc_request_t req;
            memset(&req, 0, sizeof(req));
            req.request_id = batch * batch_size + i;
            req.service_id = "echo";
            req.method_id = "echo";
            req.request_data = test_data;
            req.request_data_size = payload_size;
            
            uint8_t* serialized = NULL;
            size_t size = 0;
            uvrpc_serialize_request_msgpack(&req, &serialized, &size);
            
            uvrpc_client_call_async(client, "echo", "echo", serialized, size, asyncs[i]);
            uvrpc_free_serialized_data(serialized);
        }
        
        /* 等待所有请求完成 */
        uvrpc_await_all(asyncs, current_batch_size);
        
        /* 统计成功数 */
        for (int i = 0; i < current_batch_size; i++) {
            if (asyncs[i]->result.status == 0) {
                success_count++;
            }
            uvrpc_async_free(asyncs[i]);
        }
        
        free(asyncs);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_nsec - start.tv_nsec) / 1000000.0;
    
    printf("  总请求数: %d\n", num_requests);
    printf("  成功: %d\n", success_count);
    printf("  总耗时: %.3f ms\n", elapsed_ms);
    printf("  吞吐量: %.0f ops/s\n", (num_requests / elapsed_ms) * 1000);
    printf("  平均延迟: %.3f ms\n", elapsed_ms / concurrency);
    printf("\n");
    
    free(test_data);
}

int main() {
    setbuf(stdout, NULL);
    
    printf("========================================\n");
    printf("  UVRPC Inproc 性能测试\n");
    printf("========================================\n\n");
    
    int num_requests = 100;
    int concurrency = 20;
    int payload_size = 128;
    
    printf("测试参数:\n");
    printf("  总请求数: %d\n", num_requests);
    printf("  并发数: %d\n", concurrency);
    printf("  负载大小: %d bytes\n", payload_size);
    printf("\n");
    
    /* 创建共享的ZMQ context */
    printf("创建共享的ZMQ context...\n");
    void* shared_ctx = zmq_ctx_new();
    if (!shared_ctx) {
        fprintf(stderr, "Failed to create ZMQ context\n");
        return 1;
    }
    printf("ZMQ context创建成功\n\n");
    
    /* ==================== 测试1: 独立loop，同一线程（回调模式） ==================== */
    printf("========================================\n");
    printf("  测试1: 独立loop，同一线程\n");
    printf("========================================\n\n");
    
    uv_loop_t server_loop1, client_loop1;
    uv_loop_init(&server_loop1);
    uv_loop_init(&client_loop1);
    
    uvrpc_server_t* server1 = uvrpc_server_new_with_ctx(&server_loop1, "inproc://uvrpc-test1", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_server_register_service(server1, "echo", echo_handler, NULL);
    uvrpc_server_start(server1);
    
    uvrpc_client_t* client1 = uvrpc_client_new_with_ctx(&client_loop1, "inproc://uvrpc-test1", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_client_connect(client1);
    
    test_callback_single_thread(client1, &client_loop1, &server_loop1, num_requests, payload_size);
    
    uvrpc_client_free(client1);
    uvrpc_server_free(server1);
    uv_loop_close(&client_loop1);
    uv_loop_close(&server_loop1);
    
    /* ==================== 测试2: 共享loop，单线程（内联调用） ==================== */
    printf("========================================\n");
    printf("  测试2: 共享loop，单线程（内联调用）\n");
    printf("========================================\n\n");
    
    uv_loop_t loop2;
    uv_loop_init(&loop2);
    
    uvrpc_server_t* server2 = uvrpc_server_new_with_ctx(&loop2, "inproc://uvrpc-test2", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_server_register_service(server2, "echo", echo_handler, NULL);
    uvrpc_server_start(server2);
    
    uvrpc_client_t* client2 = uvrpc_client_new_with_ctx(&loop2, "inproc://uvrpc-test2", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_client_connect(client2);
    
    test_inline_call(client2, &loop2, num_requests, payload_size);
    
    uvrpc_client_free(client2);
    uvrpc_server_free(server2);
    uv_loop_close(&loop2);
    
    /* ==================== 测试3: 共享loop，单线程（并发await） ==================== */
    printf("========================================\n");
    printf("  测试3: 共享loop，单线程（并发await）\n");
    printf("========================================\n\n");
    
    uv_loop_t loop3;
    uv_loop_init(&loop3);
    
    uvrpc_server_t* server3 = uvrpc_server_new_with_ctx(&loop3, "inproc://uvrpc-test3", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_server_register_service(server3, "echo", echo_handler, NULL);
    uvrpc_server_start(server3);
    
    uvrpc_client_t* client3 = uvrpc_client_new_with_ctx(&loop3, "inproc://uvrpc-test3", UVRPC_MODE_ROUTER_DEALER, shared_ctx);
    uvrpc_client_connect(client3);
    
    test_concurrent_await_shared_loop(client3, &loop3, num_requests, concurrency, payload_size);
    
    uvrpc_client_free(client3);
    uvrpc_server_free(server3);
    uv_loop_close(&loop3);
    
    /* 清理 */
    printf("清理资源...\n");
    zmq_ctx_term(shared_ctx);
    printf("ZMQ context已释放\n\n");
    
    printf("========================================\n");
    printf("  所有测试完成\n");
    printf("========================================\n");
    
    return 0;
}