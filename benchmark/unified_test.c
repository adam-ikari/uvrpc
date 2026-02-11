#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SERVER_ADDR "tcp://127.0.0.1:6002"
#define DEFAULT_NUM_REQUESTS 1000
#define DEFAULT_PAYLOAD_SIZE 128

/* Echo 服务处理器 */
int echo_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;
    *response_data = (uint8_t*)UVRPC_MALLOC(request_size);
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    return UVRPC_OK;
}

/* 服务器线程 */
void* server_thread(void* arg) {
    (void)arg;
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, DEFAULT_SERVER_ADDR, UVRPC_MODE_ROUTER_DEALER);
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    return NULL;
}

/* 串行 await 测试 */
double test_serial_await(uvrpc_client_t* client, int num_requests, uint8_t* test_data, int payload_size) {
    printf("  [串行 await] 测试 %d 个请求...\n", num_requests);
    
    uvrpc_async_t* async = uvrpc_async_new(client->loop);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < num_requests; i++) {
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = i;
        req.service_id = "echo";
        req.method_id = "echo";
        req.request_data = test_data;
        req.request_data_size = payload_size;
        
        uint8_t* serialized = NULL;
        size_t size = 0;
        uvrpc_serialize_request_msgpack(&req, &serialized, &size);
        
        uvrpc_async_result_t result;
        UVRPC_AWAIT(result, async, client, "echo", "echo", serialized, size);
        (void)result;  /* Suppress unused warning */
        
        uvrpc_free_serialized_data(serialized);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    uvrpc_async_free(async);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("  [串行 await] 总耗时: %.2f ms, 吞吐量: %.2f ops/s\n", elapsed_ms, (num_requests / elapsed_ms) * 1000);
    
    return elapsed_ms;
}

/* 并发 await 测试 */
double test_concurrent_await(uvrpc_client_t* client, int num_requests, int concurrency, uint8_t* test_data, int payload_size) {
    printf("  [并发 await] 测试 %d 个请求，并发数: %d...\n", num_requests, concurrency);
    
    int batch_size = concurrency;
    int batches = (num_requests + batch_size - 1) / batch_size;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int batch = 0; batch < batches; batch++) {
        int current_batch_size = (batch == batches - 1) ? 
            (num_requests - batch * batch_size) : batch_size;
        
        uvrpc_async_t** asyncs = (uvrpc_async_t**)UVRPC_MALLOC(current_batch_size * sizeof(uvrpc_async_t*));
        for (int i = 0; i < current_batch_size; i++) {
            asyncs[i] = uvrpc_async_new(client->loop);
        }
        
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
        
        uvrpc_await_all(asyncs, current_batch_size);
        
        for (int i = 0; i < current_batch_size; i++) {
            uvrpc_async_free(asyncs[i]);
        }
        UVRPC_FREE(asyncs);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("  [并发 await] 总耗时: %.2f ms, 吞吐量: %.2f ops/s\n", elapsed_ms, (num_requests / elapsed_ms) * 1000);
    
    return elapsed_ms;
}

/* 回调模式测试 */
double test_callback(uvrpc_client_t* client, int num_requests, uint8_t* test_data, int payload_size) {
    printf("  [回调模式] 测试 %d 个请求...\n", num_requests);
    
    volatile int completed = 0;
    volatile int succeeded = 0;
    
    void callback(void* ctx, int status,
                  const uint8_t* response_data,
                  size_t response_size) {
        volatile int* completed_ptr = (volatile int*)ctx;
        (void)response_data;
        (void)response_size;
        
        if (status == UVRPC_OK) {
            succeeded++;
        }
        (*completed_ptr)++;
    }
    
    /* 一次性发送所有请求 */
    for (int i = 0; i < num_requests; i++) {
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = i;
        req.service_id = "echo";
        req.method_id = "echo";
        req.request_data = test_data;
        req.request_data_size = payload_size;
        
        uint8_t* serialized = NULL;
        size_t size = 0;
        uvrpc_serialize_request_msgpack(&req, &serialized, &size);
        
        uvrpc_client_call(client, "echo", "echo", serialized, size, callback, (void*)&completed);
        uvrpc_free_serialized_data(serialized);
    }
    
    /* 等待所有响应 */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (completed < num_requests) {
        uv_run(client->loop, UV_RUN_ONCE);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("  [回调模式] 总耗时: %.2f ms, 吞吐量: %.2f ops/s, 成功: %d/%d\n", 
           elapsed_ms, (num_requests / elapsed_ms) * 1000, succeeded, num_requests);
    
    return elapsed_ms;
}

int main(int argc, char** argv) {
    int num_requests = (argc > 1) ? atoi(argv[1]) : DEFAULT_NUM_REQUESTS;
    int concurrency = (argc > 2) ? atoi(argv[2]) : 10;
    int payload_size = (argc > 3) ? atoi(argv[3]) : DEFAULT_PAYLOAD_SIZE;
    
    printf("========================================\n");
    printf("  UVRPC 统一性能测试\n");
    printf("========================================\n");
    printf("请求数: %d\n", num_requests);
    printf("并发数: %d\n", concurrency);
    printf("负载大小: %d bytes\n", payload_size);
    printf("========================================\n\n");
    
    /* 启动服务器 */
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, server_thread, NULL);
    sleep(1);
    
    /* 创建客户端 */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, DEFAULT_SERVER_ADDR, UVRPC_MODE_ROUTER_DEALER);
    uvrpc_client_connect(client);
    
    /* 准备测试数据 */
    uint8_t* test_data = (uint8_t*)UVRPC_MALLOC(payload_size);
    memset(test_data, 'A', payload_size);
    
    /* 运行测试 */
    double serial_time = test_serial_await(client, num_requests, test_data, payload_size);
    printf("\n");
    double concurrent_time = test_concurrent_await(client, num_requests, concurrency, test_data, payload_size);
    printf("\n");
    double callback_time = test_callback(client, num_requests, test_data, payload_size);
    
    /* 清理 */
    UVRPC_FREE(test_data);
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    /* 总结 */
    printf("\n========================================\n");
    printf("  性能对比总结\n");
    printf("========================================\n");
    printf("串行 await: %.2f ms (%.2f ops/s)\n", serial_time, (num_requests / serial_time) * 1000);
    printf("并发 await: %.2f ms (%.2f ops/s) - %.1fx 提升\n", concurrent_time, (num_requests / concurrent_time) * 1000, serial_time / concurrent_time);
    printf("回调模式:   %.2f ms (%.2f ops/s) - %.1fx 提升\n", callback_time, (num_requests / callback_time) * 1000, serial_time / callback_time);
    printf("========================================\n");
    
    return 0;
}