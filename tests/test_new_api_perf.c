/**
 * UVRPC 新API 真实性能测试
 * 使用独立的事件循环测试（真实分布式场景）
 */

#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <mpack.h>

/* 测试配置 */
#define TEST_ITERATIONS 10000
#define TEST_PAYLOAD_SIZE 1024

/* 共享状态 */
typedef struct {
    pthread_mutex_t mutex;
    int completed;
    int succeeded;
    int failed;
    pthread_cond_t cond;
} shared_state_t;

static shared_state_t g_shared_state;

/* 测试处理器 - Echo */
int echo_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;
    /* 直接返回请求作为响应 */
    *response_data = malloc(request_size);
    if (*response_data) {
        memcpy(*response_data, request_data, request_size);
        *response_size = request_size;
    } else {
        *response_size = 0;
    }
    return UVRPC_OK;
}

/* 创建测试数据 */
static uint8_t* create_test_data(size_t size) {
    uint8_t* data = malloc(size);
    if (data) {
        for (size_t i = 0; i < size; i++) {
            data[i] = (uint8_t)(i % 256);
        }
    }
    return data;
}

/* 序列化请求 */
static int serialize_request(const char* service, const char* method,
                             const uint8_t* data, size_t data_size,
                             uint8_t** out, size_t* out_size) {
    uvrpc_request_t req;
    req.request_id = 1;
    req.service_id = (char*)service;
    req.method_id = (char*)method;
    req.request_data = (uint8_t*)data;
    req.request_data_size = data_size;

    return uvrpc_serialize_request_msgpack(&req, out, out_size);
}

/* 测试结果结构 */
typedef struct {
    double ops_per_second;
    double avg_latency_us;
    int success_count;
    int error_count;
} test_result_t;

/* 服务器线程函数 */
typedef struct {
    uv_loop_t* loop;
    uvrpc_server_t* server;
    int port;
    uvrpc_mode_t mode;
    int is_running;
} server_thread_arg_t;

void* server_thread_func(void* arg) {
    server_thread_arg_t* sarg = (server_thread_arg_t*)arg;

    char server_addr[64];
    snprintf(server_addr, sizeof(server_addr), "tcp://127.0.0.1:%d", sarg->port);

    /* 创建ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建服务器配置 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, sarg->loop);
    uvrpc_config_set_address(server_config, server_addr);
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(server_config, sarg->mode);
    uvrpc_config_set_zmq_ctx(server_config, zmq_ctx);
    uvrpc_config_set_hwm(server_config, 10000, 10000);

    /* 创建服务器 */
    sarg->server = uvrpc_server_create(server_config);
    if (!sarg->server) {
        printf("Failed to create server\n");
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    /* 注册服务 */
    uvrpc_server_register_service(sarg->server, "echo", echo_handler, NULL);

    /* 启动服务器 */
    uvrpc_server_start(sarg->server);
    sarg->is_running = 1;

    printf("[Server] Started on %s\n", server_addr);
    fflush(stdout);

    /* 运行事件循环 */
    while (sarg->is_running) {
        uv_run(sarg->loop, UV_RUN_ONCE);
    }

    /* 清理 */
    uvrpc_server_free(sarg->server);
    uvrpc_config_free(server_config);
    zmq_ctx_term(zmq_ctx);

    return NULL;
}

/* 客户端响应回调 */
void client_callback(void* ctx, int status, const uint8_t* response_data, size_t response_size) {
    (void)response_data;
    (void)response_size;

    shared_state_t* state = (shared_state_t*)ctx;

    pthread_mutex_lock(&state->mutex);
    state->completed++;
    if (status == UVRPC_OK) {
        state->succeeded++;
    } else {
        state->failed++;
    }
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

/* SERVER_CLIENT 模式测试（独立loop） */
static test_result_t test_server_client_tcp_real(int iterations, int payload_size) {
    test_result_t result = {0, 0, 0, 0};
    int port = 15560;

    /* 初始化共享状态 */
    pthread_mutex_init(&g_shared_state.mutex, NULL);
    pthread_cond_init(&g_shared_state.cond, NULL);
    g_shared_state.completed = 0;
    g_shared_state.succeeded = 0;
    g_shared_state.failed = 0;

    /* 创建服务器线程 */
    pthread_t server_thread;
    server_thread_arg_t server_arg = {0};
    server_arg.loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(server_arg.loop);
    server_arg.port = port;
    server_arg.mode = UVRPC_SERVER_CLIENT;
    server_arg.is_running = 0;

    pthread_create(&server_thread, NULL, server_thread_func, &server_arg);

    /* 等待服务器启动 */
    while (!server_arg.is_running) {
        usleep(1000);
    }
    usleep(100000);  /* 额外等待确保完全启动 */

    /* 创建客户端loop */
    uv_loop_t client_loop;
    uv_loop_init(&client_loop);

    char server_addr[64];
    snprintf(server_addr, sizeof(server_addr), "tcp://127.0.0.1:%d", port);

    /* 创建ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建客户端配置 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &client_loop);
    uvrpc_config_set_address(client_config, server_addr);
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(client_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(client_config, zmq_ctx);
    uvrpc_config_set_hwm(client_config, 10000, 10000);

    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("Failed to create client\n");
        server_arg.is_running = 0;
        pthread_join(server_thread, NULL);
        uv_loop_close(server_arg.loop);
        free(server_arg.loop);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        pthread_mutex_destroy(&g_shared_state.mutex);
        pthread_cond_destroy(&g_shared_state.cond);
        return result;
    }

    /* 连接服务器 */
    uvrpc_client_connect(client);

    /* 等待连接建立 */
    usleep(100000);

    /* 创建测试数据 */
    uint8_t* test_data = create_test_data(payload_size);

    /* 性能测试 */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 发送所有请求 */
    for (int i = 0; i < iterations; i++) {
        /* 序列化请求 */
        uint8_t* serialized = NULL;
        size_t serialized_size = 0;
        if (serialize_request("echo", "test", test_data, payload_size,
                           &serialized, &serialized_size) != 0) {
            result.error_count++;
            continue;
        }

        /* 发送请求 */
        int rc = uvrpc_client_call(client, "echo", "test",
                                   serialized, serialized_size,
                                   client_callback, &g_shared_state);
        uvrpc_free_serialized_data(serialized);

        if (rc != UVRPC_OK) {
            result.error_count++;
            continue;
        }
    }

    /* 持续运行事件循环处理响应 */
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;  /* 30秒超时 */

    pthread_mutex_lock(&g_shared_state.mutex);
    while (g_shared_state.completed < iterations) {
        pthread_mutex_unlock(&g_shared_state.mutex);

        /* 运行事件循环处理响应 */
        uv_run(&client_loop, UV_RUN_ONCE);

        pthread_mutex_lock(&g_shared_state.mutex);

        /* 检查超时 */
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > timeout.tv_sec ||
            (now.tv_sec == timeout.tv_sec && now.tv_nsec > timeout.tv_nsec)) {
            printf("Timeout waiting for responses\n");
            break;
        }
    }
    pthread_mutex_unlock(&g_shared_state.mutex);

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* 获取结果 */
    result.success_count = g_shared_state.succeeded;
    result.error_count = g_shared_state.failed;

    /* 计算结果 */
    double elapsed_sec = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    result.ops_per_second = result.success_count / elapsed_sec;
    result.avg_latency_us = (elapsed_sec * 1e6) / result.success_count;

    /* 清理 */
    free(test_data);
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    zmq_ctx_term(zmq_ctx);

    uv_run(&client_loop, UV_RUN_NOWAIT);
    uv_loop_close(&client_loop);

    /* 停止服务器线程 */
    server_arg.is_running = 0;
    pthread_join(server_thread, NULL);
    uv_loop_close(server_arg.loop);
    free(server_arg.loop);

    pthread_mutex_destroy(&g_shared_state.mutex);
    pthread_cond_destroy(&g_shared_state.cond);

    return result;
}

/* SERVER_CLIENT Inproc 模式测试（共享loop - 用于对比） */
static test_result_t test_server_client_inproc_shared(int iterations, int payload_size) {
    test_result_t result = {0, 0, 0, 0};
    const char* server_addr = "inproc://test_server_real";

    /* 创建libuv循环 */
    uv_loop_t* loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(loop);

    /* 创建ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建服务器配置 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, loop);
    uvrpc_config_set_address(server_config, server_addr);
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(server_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(server_config, zmq_ctx);
    uvrpc_config_set_hwm(server_config, 10000, 10000);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        printf("Failed to create server\n");
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(loop);
        free(loop);
        return result;
    }

    /* 注册服务 */
    uvrpc_server_register_service(server, "echo", echo_handler, NULL);

    /* 启动服务器 */
    uvrpc_server_start(server);

    /* 短暂等待服务器启动 */
    usleep(10000);

    /* 创建客户端配置 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, loop);
    uvrpc_config_set_address(client_config, server_addr);
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(client_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(client_config, zmq_ctx);
    uvrpc_config_set_hwm(client_config, 10000, 10000);

    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        printf("Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(loop);
        free(loop);
        return result;
    }

    /* 连接服务器 */
    uvrpc_client_connect(client);

    /* 等待连接建立 */
    usleep(10000);

    /* 创建测试数据 */
    uint8_t* test_data = create_test_data(payload_size);

    /* 性能测试 */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        /* 序列化请求 */
        uint8_t* serialized = NULL;
        size_t serialized_size = 0;
        if (serialize_request("echo", "test", test_data, payload_size,
                           &serialized, &serialized_size) != 0) {
            result.error_count++;
            continue;
        }

        /* 使用async方式调用 */
        uvrpc_async_t* async = uvrpc_async_create(loop);
        if (!async) {
            uvrpc_free_serialized_data(serialized);
            result.error_count++;
            continue;
        }

        /* 发送请求 */
        int rc = uvrpc_client_call_async(client, "echo", "test",
                                        serialized, serialized_size, async);
        uvrpc_free_serialized_data(serialized);

        if (rc != UVRPC_OK) {
            uvrpc_async_free(async);
            result.error_count++;
            continue;
        }

        /* 等待响应 */
        const uvrpc_async_result_t* res = uvrpc_async_await(async);
        uvrpc_async_free(async);

        if (res && res->status == UVRPC_OK) {
            result.success_count++;
        } else {
            result.error_count++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    /* 计算结果 */
    double elapsed_sec = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    result.ops_per_second = result.success_count / elapsed_sec;
    result.avg_latency_us = (elapsed_sec * 1e6) / result.success_count;

    /* 清理 */
    free(test_data);
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    zmq_ctx_term(zmq_ctx);

    /* 运行事件循环清理 */
    uv_run(loop, UV_RUN_NOWAIT);
    uv_loop_close(loop);
    free(loop);

    return result;
}

int main(int argc, char** argv) {
    printf("========================================\n");
    printf("UVRPC New API Real Performance Test\n");
    printf("(Independent event loops)\n");
    printf("========================================\n\n");

    int iterations = TEST_ITERATIONS;
    int payload_size = TEST_PAYLOAD_SIZE;

    if (argc > 1) {
        iterations = atoi(argv[1]);
    }
    if (argc > 2) {
        payload_size = atoi(argv[2]);
    }

    printf("Iterations: %d\n", iterations);
    printf("Payload Size: %d bytes\n\n", payload_size);

    /* 测试1: SERVER_CLIENT + TCP (独立loop) - 真实场景 */
    printf("Test 1: SERVER_CLIENT (ROUTER/DEALER) + TCP [Independent Loops - REAL]\n");
    printf("----------------------------------------------------------------------------\n");
    test_result_t result1 = test_server_client_tcp_real(iterations, payload_size);
    printf("Ops/sec: %.2f\n", result1.ops_per_second);
    printf("Avg Latency: %.2f us\n", result1.avg_latency_us);
    printf("Success: %d, Errors: %d\n\n", result1.success_count, result1.error_count);

    /* 测试2: SERVER_CLIENT + Inproc (共享loop) - 最佳场景对比 */
    printf("Test 2: SERVER_CLIENT (ROUTER/DEALER) + Inproc [Shared Loop - BEST]\n");
    printf("----------------------------------------------------------------------------\n");
    test_result_t result2 = test_server_client_inproc_shared(iterations, payload_size);
    printf("Ops/sec: %.2f\n", result2.ops_per_second);
    printf("Avg Latency: %.2f us\n", result2.avg_latency_us);
    printf("Success: %d, Errors: %d\n\n", result2.success_count, result2.error_count);

    printf("========================================\n");
    printf("Performance Comparison:\n");
    printf("========================================\n");
    printf("Independent Loops (REAL): %.2f ops/sec, %.2f us latency\n", result1.ops_per_second, result1.avg_latency_us);
    printf("Shared Loop (BEST):       %.2f ops/sec, %.2f us latency\n", result2.ops_per_second, result2.avg_latency_us);
    printf("Performance Ratio:       %.2fx\n", result2.ops_per_second / result1.ops_per_second);

    return 0;
}
