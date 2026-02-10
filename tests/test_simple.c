#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* 简单的 echo 服务处理器 */
int echo_handler(void* ctx, const uint8_t* request_data, size_t request_size,
                 uint8_t** response_data, size_t* response_size) {
    (void)ctx;
    /* 直接返回请求数据 */
    *response_data = (uint8_t*)malloc(request_size);
    if (*response_data) {
        memcpy(*response_data, request_data, request_size);
        *response_size = request_size;
        return UVRPC_OK;
    }
    return UVRPC_ERROR_NO_MEMORY;
}

/* 服务器线程函数 */
void* server_thread(void* arg) {
    (void)arg;
    printf("Starting ROUTER server...\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:6005", UVRPC_MODE_ROUTER_DEALER);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return NULL;
    }
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        return NULL;
    }
    
    printf("Server running...\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    
    return NULL;
}

/* 客户端测试 */
void client_test() {
    printf("Starting DEALER client...\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://127.0.0.1:6005", UVRPC_MODE_ROUTER_DEALER);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect\n");
        uvrpc_client_free(client);
        return;
    }
    
    printf("Client connected\n");
    
    /* 发送测试请求 */
    const char* test_data = "Hello, ROUTER/DEALER!";
    int completed = 0;
    
    void callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
        int* completed = (int*)ctx;
        printf("Callback: status=%d, data=%.*s\n", status, (int)response_size, (char*)response_data);
        *completed = 1;
    }
    
    int rc = uvrpc_client_call(client, "echo", "test", (const uint8_t*)test_data, strlen(test_data),
                               callback, &completed);
    if (rc != UVRPC_OK) {
        printf("Failed to call: %d (%s)\n", rc, uvrpc_strerror(rc));
    } else {
        printf("Call sent successfully\n");
    }
    
    /* 等待响应（最多5秒） */
    for (int i = 0; i < 500 && !completed; i++) {
        uv_run(&loop, UV_RUN_ONCE);
        usleep(10000);
    }
    
    if (completed) {
        printf("SUCCESS: Received response\n");
    } else {
        printf("FAILED: Timeout waiting for response\n");
    }
    
    uvrpc_client_free(client);
    uv_loop_close(&loop);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    /* 启动服务器线程 */
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, server_thread, NULL);
    
    /* 等待服务器启动 */
    sleep(1);
    
    /* 运行客户端测试 */
    client_test();
    
    /* 停止服务器 */
    printf("Stopping server...\n");
    /* 注意：简单实现，实际应该使用更优雅的停止机制 */
    
    return 0;
}