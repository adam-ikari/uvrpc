/**
 * UVRPC Unified Test
 * Uses new API with uvrpc_config_t
 * Single-threaded event loop model - no locks needed
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SERVER_ADDR "inproc://uvrpc_test"
#define DEFAULT_NUM_REQUESTS 100
#define DEFAULT_PAYLOAD_SIZE 128
#define WARMUP_REQUESTS 50
#define DEFAULT_TIMEOUT_MS 30000

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

/* 服务器线程参数 */
typedef struct {
    volatile int* running;
    const char* bind_addr;
} server_thread_arg_t;

/* 服务器线程 */
void* server_thread(void* arg) {
    server_thread_arg_t* sarg = (server_thread_arg_t*)arg;

    /* 创建事件循环 */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* 创建 ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建服务器配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, sarg->bind_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 1000, 1000);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    /* 注册服务 */
    if (uvrpc_server_register_service(server, "echo", echo_handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "Failed to register service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    /* 启动服务器 */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    printf("[Server] Started on %s\n", sarg->bind_addr);
    *sarg->running = 1;

    /* 运行事件循环 */
    while (*sarg->running) {
        uv_run(&loop, UV_RUN_ONCE);
    }

    /* 清理 */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);
    uv_loop_close(&loop);

    printf("[Server] Stopped\n");
    return NULL;
}

/* 预热阶段 */
static void warmup(uvrpc_client_t* client, uv_loop_t* loop, uint8_t* test_data, int payload_size) {
    printf("预热阶段 (%d 请求)...\n", WARMUP_REQUESTS);

    for (int i = 0; i < WARMUP_REQUESTS; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size, NULL, NULL);
    }

    /* 运行事件循环处理预热请求 */
    for (int i = 0; i < WARMUP_REQUESTS * 2; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("预热完成\n\n");
}

/* 测试1: 基本功能测试 */
static int test_basic(uvrpc_client_t* client, uv_loop_t* loop, uint8_t* test_data, int payload_size) {
    printf("[测试1] 基本功能测试\n");

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

    /* 发送请求 */
    uvrpc_client_call(client, "echo", "echo", test_data, payload_size,
                      callback, (void*)&completed);

    /* 等待响应 - 带超时 */
    int timeout = 1000;
    while (completed < 1 && timeout > 0) {
        uv_run(loop, UV_RUN_ONCE);
        usleep(1000);
        timeout--;
    }

    if (succeeded) {
        printf("  结果: 成功\n");
        return 0;
    } else {
        printf("  结果: 失败\n");
        return 1;
    }
}

/* 测试2: 串行请求测试 */
static int test_serial(uvrpc_client_t* client, uv_loop_t* loop, int num_requests, uint8_t* test_data, int payload_size) {
    printf("[测试2] 串行请求测试 (%d 请求)\n", num_requests);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int success_count = 0;
    for (int i = 0; i < num_requests; i++) {
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

        uvrpc_client_call(client, "echo", "echo", test_data, payload_size,
                          callback, (void*)&completed);

        /* 等待响应 - 带超时 */
        int timeout = 5000;
        while (completed < 1 && timeout > 0) {
            uv_run(loop, UV_RUN_ONCE);
            usleep(1000);
            timeout--;
        }

        if (succeeded) {
            success_count++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    printf("  成功: %d/%d\n", success_count, num_requests);
    printf("  总耗时: %.2f ms\n", elapsed_ms);
    printf("  吞吐量: %.2f ops/s\n", (num_requests / elapsed_ms) * 1000);

    return (success_count == num_requests) ? 0 : 1;
}

/* 测试3: 并发请求测试 */
static int test_concurrent(uvrpc_client_t* client, uv_loop_t* loop, int num_requests, uint8_t* test_data, int payload_size) {
    printf("[测试3] 并发请求测试 (%d 请求)\n", num_requests);

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

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 一次性发送所有请求 */
    for (int i = 0; i < num_requests; i++) {
        uvrpc_client_call(client, "echo", "echo", test_data, payload_size,
                          callback, (void*)&completed);
    }

    /* 等待所有响应 - 带超时保护 */
    struct timespec timeout_end;
    timeout_end.tv_sec = start.tv_sec + DEFAULT_TIMEOUT_MS / 1000;
    timeout_end.tv_nsec = start.tv_nsec + (DEFAULT_TIMEOUT_MS % 1000) * 1000000;
    if (timeout_end.tv_nsec >= 1000000000) {
        timeout_end.tv_sec++;
        timeout_end.tv_nsec -= 1000000000;
    }

    while (completed < num_requests) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > timeout_end.tv_sec ||
            (now.tv_sec == timeout_end.tv_sec && now.tv_nsec > timeout_end.tv_nsec)) {
            printf("  超时等待响应 (%d/%d 完成)\n", completed, num_requests);
            break;
        }
        uv_run(loop, UV_RUN_ONCE);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    printf("  成功: %d/%d\n", succeeded, num_requests);
    printf("  总耗时: %.2f ms\n", elapsed_ms);
    printf("  吞吐量: %.2f ops/s\n", (num_requests / elapsed_ms) * 1000);

    return (succeeded == num_requests) ? 0 : 1;
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : DEFAULT_SERVER_ADDR;
    int num_requests = (argc > 2) ? atoi(argv[2]) : DEFAULT_NUM_REQUESTS;
    int payload_size = (argc > 3) ? atoi(argv[3]) : DEFAULT_PAYLOAD_SIZE;

    printf("========================================\n");
    printf("  UVRPC Unified Test\n");
    printf("========================================\n");
    printf("服务器地址:    %s\n", server_addr);
    printf("请求数:        %d\n", num_requests);
    printf("负载大小:      %d bytes\n", payload_size);
    printf("========================================\n\n");

    /* 启动服务器线程 */
    volatile int server_running = 0;
    server_thread_arg_t server_arg;
    server_arg.running = &server_running;
    server_arg.bind_addr = server_addr;

    pthread_t server_tid;
    if (pthread_create(&server_tid, NULL, server_thread, &server_arg) != 0) {
        fprintf(stderr, "Failed to create server thread\n");
        return 1;
    }

    /* 等待服务器启动 */
    while (!server_running) {
        usleep(10000);
    }
    usleep(100000);

    /* 创建客户端 */
    uv_loop_t loop;
    uv_loop_init(&loop);

    void* zmq_ctx = zmq_ctx_new();

    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 1000, 1000);

    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        server_running = 0;
        pthread_join(server_tid, NULL);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        server_running = 0;
        pthread_join(server_tid, NULL);
        zmq_ctx_term(zmq_ctx);
        return 1;
    }

    printf("[Client] Connected\n\n");

    /* 准备测试数据 */
    uint8_t* test_data = (uint8_t*)malloc(payload_size);
    memset(test_data, 'A', payload_size);

    /* 运行事件循环让连接建立 */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* 预热 */
    warmup(client, &loop, test_data, payload_size);

    /* 运行测试 */
    int failures = 0;
    failures += test_basic(client, &loop, test_data, payload_size);
    printf("\n");
    failures += test_serial(client, &loop, num_requests, test_data, payload_size);
    printf("\n");
    failures += test_concurrent(client, &loop, num_requests, test_data, payload_size);

    /* 清理 */
    free(test_data);
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    /* 停止服务器 */
    server_running = 0;
    pthread_join(server_tid, NULL);

    /* 总结 */
    printf("\n========================================\n");
    printf("  测试总结\n");
    printf("========================================\n");
    if (failures == 0) {
        printf("所有测试通过！\n");
    } else {
        printf("有 %d 个测试失败\n", failures);
    }
    printf("========================================\n");

    return failures;
}