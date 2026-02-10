#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(int argc, char** argv) {
    int is_server = (argc > 1 && strcmp(argv[1], "server") == 0);
    
    if (is_server) {
        /* ROUTER 服务器 */
        printf("Starting ROUTER server on tcp://127.0.0.1:6001\n");
        
        uv_loop_t loop;
        uv_loop_init(&loop);
        
        uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://127.0.0.1:6001", UVRPC_MODE_ROUTER_DEALER);
        if (!server) {
            fprintf(stderr, "Failed to create server\n");
            return 1;
        }
        
        uvrpc_server_register_service(server, "echo", echo_handler, NULL);
        
        if (uvrpc_server_start(server) != UVRPC_OK) {
            fprintf(stderr, "Failed to start server\n");
            uvrpc_server_free(server);
            return 1;
        }
        
        printf("Server running...\n");
        uv_run(&loop, UV_RUN_DEFAULT);
        
        uvrpc_server_free(server);
        uv_loop_close(&loop);
        
    } else {
        /* DEALER 客户端 */
        printf("Starting DEALER client\n");
        
        uv_loop_t loop;
        uv_loop_init(&loop);
        
        uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://127.0.0.1:6001", UVRPC_MODE_ROUTER_DEALER);
        if (!client) {
            fprintf(stderr, "Failed to create client\n");
            return 1;
        }
        
        if (uvrpc_client_connect(client) != UVRPC_OK) {
            fprintf(stderr, "Failed to connect\n");
            uvrpc_client_free(client);
            return 1;
        }
        
        printf("Client connected\n");
        
        /* 发送测试请求 */
        const char* test_data = "Hello, ROUTER/DEALER!";
        int completed = 0;
        
        void callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
            (void)ctx;
            (void)status;
            printf("Received response: %.*s\n", (int)response_size, (char*)response_data);
            completed = 1;
        }
        
        uvrpc_client_call(client, "echo", "test", (const uint8_t*)test_data, strlen(test_data),
                         callback, NULL);
        
        /* 等待响应（最多5秒） */
        int max_iterations = 500;
        for (int i = 0; i < max_iterations && !completed; i++) {
            uv_run(&loop, UV_RUN_ONCE);
            usleep(10000);
        }
        
        if (!completed) {
            printf("Timeout waiting for response\n");
        }
        
        uvrpc_client_free(client);
        uv_loop_close(&loop);
    }
    
    return 0;
}