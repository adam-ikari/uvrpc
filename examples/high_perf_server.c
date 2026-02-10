#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 高性能 Echo 服务处理器 */
int high_perf_echo_handler(void* ctx,
                             const uint8_t* request_data,
                             size_t request_size,
                             uint8_t** response_data,
                             size_t* response_size) {
    (void)ctx;
    
    /* 直接返回请求数据（零拷贝） */
    *response_data = (uint8_t*)malloc(request_size);
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    
    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "tcp://0.0.0.0:6000";
    
    printf("========================================\n");
    printf("高性能 RPC 服务器 (ROUTER 模式)\n");
    printf("========================================\n");
    printf("绑定地址: %s\n", bind_addr);
    printf("模式: ROUTER/DEALER\n");
    printf("========================================\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建 ROUTER 服务器 */
    uvrpc_server_t* server = uvrpc_server_new(&loop, bind_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    /* 注册服务 */
    if (uvrpc_server_register_service(server, "echo", high_perf_echo_handler, NULL) != UVRPC_OK) {
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
    
    printf("✓ 服务器已启动，等待连接...\n");
    printf("按 Ctrl+C 停止\n\n");
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    
    printf("\n✓ 服务器已停止\n");
    
    return 0;
}
