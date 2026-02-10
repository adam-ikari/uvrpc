#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>

/* 性能测试配置 */
#define TEST_PORT 6010
#define NUM_REQUESTS 100
#define PAYLOAD_SIZE 128
#define TIMEOUT_SECONDS 10

/* 全局信号标志 */
static volatile sig_atomic_t g_stop_requested = 0;

/* 全局服务器循环指针 */
static uv_loop_t* g_server_loop = NULL;
static volatile sig_atomic_t g_server_running = 1;

/* 信号处理函数 - 异步信号安全 */
void signal_handler(int sig) {
    (void)sig;
    g_stop_requested = 1;
    g_server_running = 0;
}

/* 服务处理器 */
int echo_handler(void* ctx, const uint8_t* request_data, size_t request_size,
                 uint8_t** response_data, size_t* response_size) {
    (void)ctx;
    *response_data = (uint8_t*)malloc(request_size);
    if (!*response_data) return UVRPC_ERROR_NO_MEMORY;
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    return UVRPC_OK;
}

/* 服务器模式 */
void run_server_mode(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    g_server_loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
    if (!g_server_loop) {
        fprintf(stderr, "[SERVER] 内存分配失败\n");
        return;
    }
    uv_loop_init(g_server_loop);
    
    char bind_addr[64];
    snprintf(bind_addr, sizeof(bind_addr), "tcp://127.0.0.1:%d", TEST_PORT);
    
    uvrpc_server_t* server = uvrpc_server_new(g_server_loop, bind_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!server) {
        fprintf(stderr, "[SERVER] 创建服务器失败\n");
        uv_loop_close(g_server_loop);
        free(g_server_loop);
        return;
    }
    
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[SERVER] 启动服务器失败\n");
        uvrpc_server_free(server);
        uv_loop_close(g_server_loop);
        free(g_server_loop);
        return;
    }
    
    fprintf(stderr, "[SERVER] 服务器已启动，监听 %s\n", bind_addr);
    fprintf(stderr, "[SERVER] 按 Ctrl+C 停止服务器\n\n");
    
    /* 单线程事件循环 - 不需要任何锁 */
    while (g_server_running) {
        uv_run(g_server_loop, UV_RUN_ONCE);
    }
    
    fprintf(stderr, "\n[SERVER] 服务器停止\n");
    uvrpc_server_free(server);
    uv_loop_close(g_server_loop);
    free(g_server_loop);
    g_server_loop = NULL;
}

/* 客户端模式 */
void run_client_mode(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    fprintf(stderr, "[CLIENT] 开始创建客户端...\n");
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    char server_addr[64];
    snprintf(server_addr, sizeof(server_addr), "tcp://127.0.0.1:%d", TEST_PORT);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
    if (!client) {
        fprintf(stderr, "[CLIENT] 创建客户端失败\n");
        return;
    }
    
    fprintf(stderr, "[CLIENT] 开始连接服务器 %s...\n", server_addr);
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] 连接服务器失败\n");
        uvrpc_client_free(client);
        return;
    }
    
    fprintf(stderr, "[CLIENT] 已连接，准备测试\n\n");
    
    uint8_t* payload = (uint8_t*)malloc(PAYLOAD_SIZE);
    memset(payload, 'A', PAYLOAD_SIZE);
    
    /* 普通变量 - 单线程事件循环不需要原子操作 */
    int completed = 0;
    int succeeded = 0;
    int failed = 0;
    
    /* 回调上下文结构 */
    typedef struct {
        int* completed;
        int* succeeded;
        int* failed;
    } callback_ctx_t;
    
    callback_ctx_t cb_ctx = {&completed, &succeeded, &failed};
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    /* 回调函数 - 单线程执行，无需锁 */
    void callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
        (void)response_data;
        (void)response_size;
        
        callback_ctx_t* cb = (callback_ctx_t*)ctx;
        if (cb && cb->completed) {
            (*cb->completed)++;
        }
        
        if (status == UVRPC_OK) {
            if (cb && cb->succeeded) {
                (*cb->succeeded)++;
            }
        } else {
            if (cb && cb->failed) {
                (*cb->failed)++;
            }
        }
    }
    
    /* 发送所有请求 */
    fprintf(stderr, "[CLIENT] 开始发送 %d 个请求...\n", NUM_REQUESTS);
    int sent = 0;
    for (int i = 0; i < NUM_REQUESTS && !g_stop_requested; i++) {
        int rc = uvrpc_client_call(client, "echo", "test", payload, PAYLOAD_SIZE, callback, &cb_ctx);
        if (rc == UVRPC_OK) {
            sent++;
        } else {
            fprintf(stderr, "[CLIENT] 发送请求 %d 失败\n", i);
            failed++;
        }
    }
    fprintf(stderr, "[CLIENT] 已发送 %d 个请求，开始等待响应...\n\n", sent);
    
    /* 等待所有响应 - 单线程事件循环 */
    int iterations = 0;
    int max_iterations = 100000;
    time_t timeout_start = time(NULL);
    while (completed < NUM_REQUESTS && iterations < max_iterations && !g_stop_requested) {
        uv_run(&loop, UV_RUN_ONCE);
        iterations++;
        
        /* 检查超时 */
        if (time(NULL) - timeout_start > TIMEOUT_SECONDS) {
            fprintf(stderr, "[CLIENT] 超时！已完成 %d/%d\n", completed, NUM_REQUESTS);
            break;
        }
        
        if (iterations % 1000 == 0 && completed < NUM_REQUESTS) {
            fprintf(stderr, "[CLIENT] 进度: %d/%d (%.1f%%) - 迭代: %d\n", 
                    completed, NUM_REQUESTS, (completed * 100.0) / NUM_REQUESTS, iterations);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double total_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;
    
    printf("\n========== 性能测试结果 (单线程无锁模式) ==========\n");
    printf("总请求数: %d\n", NUM_REQUESTS);
    printf("已发送: %d\n", sent);
    printf("成功: %d\n", succeeded);
    printf("失败: %d\n", failed);
    printf("总耗时: %.2f ms\n", total_time_ms);
    if (succeeded > 0) {
        printf("吞吐量: %.2f ops/s\n", (succeeded * 1000.0) / total_time_ms);
        printf("平均延迟: %.3f ms\n", total_time_ms / NUM_REQUESTS);
    }
    printf("循环迭代: %d\n", iterations);
    if (iterations > 0) {
        printf("事件循环效率: %.2f%% (完成数/迭代数)\n", (completed * 100.0) / iterations);
    }
    printf("======================================================\n");
    
    free(payload);
    uvrpc_client_free(client);
    uv_loop_close(&loop);
}

void print_usage(const char* program_name) {
    printf("用法: %s [选项]\n", program_name);
    printf("选项:\n");
    printf("  -s, --server     运行服务器模式\n");
    printf("  -c, --client     运行客户端模式\n");
    printf("  -h, --help       显示帮助信息\n");
    printf("\n示例:\n");
    printf("  # 终端 1: 启动服务器\n");
    printf("  %s --server\n\n", program_name);
    printf("  # 终端 2: 运行客户端测试\n");
    printf("  %s --client\n\n", program_name);
}

int main(int argc, char** argv) {
    int mode = 0;  // 0 = 未指定, 1 = 服务器, 2 = 客户端
    
    static struct option long_options[] = {
        {"server", no_argument, 0, 's'},
        {"client", no_argument, 0, 'c'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "sch", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                mode = 1;
                break;
            case 'c':
                mode = 2;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (mode == 0) {
        fprintf(stderr, "错误: 必须指定运行模式\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    printf("========================================\n");
    printf("UVRPC 性能测试\n");
    printf("========================================\n");
    printf("请求数: %d\n", NUM_REQUESTS);
    printf("负载大小: %d bytes\n", PAYLOAD_SIZE);
    printf("模式: %s\n", mode == 1 ? "服务器" : "客户端");
    printf("========================================\n\n");
    
    if (mode == 1) {
        run_server_mode();
    } else {
        run_client_mode();
    }
    
    printf("\n程序正常退出\n");
    return 0;
}