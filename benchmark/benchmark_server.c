/**
 * UVRPC Benchmark Server
 * Uses new API with uvrpc_config_t
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

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
    *response_data = malloc(request_size);
    if (!*response_data) {
        return UVRPC_ERROR;
    }
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : DEFAULT_SERVER_ADDR;

    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("========================================\n");
    printf("  UVRPC Benchmark Server\n");
    printf("========================================\n");
    printf("绑定地址: %s\n", bind_addr);
    printf("========================================\n\n");

    /* 创建事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建服务器配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, bind_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* 注册服务 */
    if (uvrpc_server_register_service(server, "echo", echo_handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    /* 启动服务器 */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        fprintf(stderr, "Make sure the address %s is not already in use\n", bind_addr);
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    printf("Server started, waiting for requests...\n");
    printf("Press Ctrl+C to stop\n\n");

    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    printf("Server stopped\n");
    return 0;
}