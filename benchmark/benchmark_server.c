#include "../include/uvrpc.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define DEFAULT_SERVER_ADDR "tcp://127.0.0.1:6002"

volatile sig_atomic_t g_running = 1;

void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

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

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : DEFAULT_SERVER_ADDR;
    
    /* 设置信号处理 */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    printf("========================================\n");
    printf("  UVRPC Benchmark Server\n");
    printf("========================================\n");
    printf("绑定地址: %s\n", bind_addr);
    printf("========================================\n\n");
    
    /* 创建事件循环 */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_new(&loop, bind_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* 注册服务 */
    if (uvrpc_server_register_service(server, "echo", echo_handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register service\n");
        uvrpc_server_free(server);
        return 1;
    }
    
    /* 启动服务器 */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        return 1;
    }
    
    printf("Server started, waiting for requests...\n");
    printf("Press Ctrl+C to stop\n\n");
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    printf("\nServer stopping...\n");
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    
    printf("Server stopped\n");
    return 0;
}
