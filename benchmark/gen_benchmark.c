/**
 * UVRPC Generated Code Benchmark
 * Uses generated client/server code for performance testing
 * Supports all transport modes: TCP, INPROC, IPC
 * Single-threaded event loop model - no locks needed
 */

#include "../include/uvrpc.h"
#include "../generated/echoservice_gen.h"
#include "../generated/echoservice_gen_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define DEFAULT_NUM_REQUESTS 10
#define DEFAULT_CONCURRENCY 2
#define WARMUP_REQUESTS 20
#define DEFAULT_TIMEOUT_MS 10000

/* 传输类型 */
typedef enum {
    TRANSPORT_TCP,
    TRANSPORT_INPROC,
    TRANSPORT_IPC
} transport_type_t;

/* 测试结果 */
typedef struct {
    double total_time_ms;
    double throughput_ops_per_sec;
    double avg_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
    double p50_latency_ms;
    double p95_latency_ms;
    double p99_latency_ms;
    int success_count;
    int failed_count;
} test_result_t;

/* 延迟统计 */
typedef struct {
    double* latencies;
    int count;
    int capacity;
    double sum;
    double min;
    double max;
} latency_stats_t;

/* Echo服务处理器 - 使用生成的API */
int EchoService_echo_Handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* Deserialize request */
    EchoService_echo_Request_t request;
    if (EchoService_echo_DeserializeRequest(request_data, request_size, &request) != 0) {
        return UVRPC_ERROR;
    }

    /* Process request - echo back the message */
    EchoService_echo_Response_t response;
    memset(&response, 0, sizeof(response));

    /* Echo back the message */
    if (request.message) {
        response.echo = strdup(request.message);
    }
    response.timestamp = (int64_t)time(NULL);

    /* Serialize response */
    int rc = EchoService_echo_SerializeResponse(&response, response_data, response_size);

    /* Cleanup */
    EchoService_echo_FreeRequest(&request);
    EchoService_echo_FreeResponse(&response);

    return rc == 0 ? UVRPC_OK : UVRPC_ERROR;
}

/* 函数声明 */
static void run_inproc_shared_loop_test(int num_requests, int concurrency);
static void run_transport_test(const char* transport_name, const char* server_addr,
                               uvrpc_transport_t transport, int num_requests, int concurrency);
static test_result_t test_generated_api(uvrpc_client_t* client, uv_loop_t* loop,
                                      int num_requests, int concurrency);
static void warmup(uvrpc_client_t* client, uv_loop_t* loop);

/* 服务器线程参数 */
typedef struct {
    volatile int* running;
    const char* bind_addr;
    uvrpc_transport_t transport;
    uvrpc_mode_t mode;
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
    uvrpc_config_set_transport(config, sarg->transport);
    uvrpc_config_set_mode(config, sarg->mode);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);
    uvrpc_config_set_perf_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "[Server] Failed to create server\n");
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    /* 注册服务 */
    if (uvrpc_server_register_service(server, "EchoService.echo", EchoService_echo_Handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "[Server] Failed to register service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return NULL;
    }

    /* 启动服务器 */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[Server] Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return NULL;
    }

    printf("[Server] Started on %s\n", sarg->bind_addr);
    fflush(stdout);
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

static void latency_stats_init(latency_stats_t* stats, int capacity) {
    stats->latencies = (double*)malloc(capacity * sizeof(double));
    stats->count = 0;
    stats->capacity = capacity;
    stats->sum = 0.0;
    stats->min = INFINITY;
    stats->max = -INFINITY;
}

static void latency_stats_record(latency_stats_t* stats, double latency_ms) {
    if (stats->count < stats->capacity) {
        stats->latencies[stats->count] = latency_ms;
        stats->sum += latency_ms;
        if (latency_ms < stats->min) stats->min = latency_ms;
        if (latency_ms > stats->max) stats->max = latency_ms;
        stats->count++;
    }
}

static double calculate_percentile(double* data, int count, double percentile) {
    if (count == 0) return 0.0;

    /* 简单排序 */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (data[j] > data[j + 1]) {
                double temp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }

    int index = (int)ceil((percentile / 100.0) * count) - 1;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;

    return data[index];
}

/* 预热阶段 */
static void warmup(uvrpc_client_t* client, uv_loop_t* loop) {
    printf("预热阶段 (%d 请求)...\n", WARMUP_REQUESTS);

    EchoService_echo_Request_t request;
    memset(&request, 0, sizeof(request));
    request.message = "warmup";

    for (int i = 0; i < WARMUP_REQUESTS; i++) {
        EchoService_echo_CallAsync(client, &request, NULL, loop);
    }

    /* 运行事件循环处理预热请求 */
    for (int i = 0; i < WARMUP_REQUESTS * 2; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
    }

    printf("预热完成\n\n");
}

/* 测试：使用生成的API进行性能测试 */
static test_result_t test_generated_api(uvrpc_client_t* client, uv_loop_t* loop,
                                      int num_requests, int concurrency) {
    printf("[测试] 使用生成的API进行性能测试\n");
    printf("  请求数: %d, 并发数: %d\n", num_requests, concurrency);
    fflush(stdout);

    latency_stats_t stats;
    latency_stats_init(&stats, num_requests);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int batch_size = concurrency;
    int batches = (num_requests + batch_size - 1) / batch_size;

    int succeeded = 0;

    printf("[测试] Starting %d batches...\n", batches);
    fflush(stdout);

    for (int batch = 0; batch < batches; batch++) {
        printf("[测试] Batch %d/%d...\n", batch + 1, batches);
        fflush(stdout);

        int current_batch_size = (batch == batches - 1) ?
            (num_requests - batch * batch_size) : batch_size;

        /* 创建async数组 */
        uvrpc_async_t** asyncs = (uvrpc_async_t**)malloc(current_batch_size * sizeof(uvrpc_async_t*));
        struct timespec* req_starts = (struct timespec*)malloc(current_batch_size * sizeof(struct timespec));

        for (int i = 0; i < current_batch_size; i++) {
            asyncs[i] = uvrpc_async_create(loop);
        }

        /* 准备请求 */
        EchoService_echo_Request_t request;
        memset(&request, 0, sizeof(request));
        request.message = "hello, generated code benchmark!";

        /* 使用生成的API发送批量请求 */
        for (int i = 0; i < current_batch_size; i++) {
            clock_gettime(CLOCK_MONOTONIC, &req_starts[i]);
            int rc = EchoService_echo_Async(client, &request, asyncs[i]);
            if (rc != UVRPC_OK) {
                printf("[测试] Failed to send request %d\n", batch * batch_size + i);
            }
        }

        /* 运行事件循环处理异步响应 - INPROC模式下可以批量处理 */
        int loops = 0;
        while (loops < 1000) {  /* 平衡处理速度和性能 */
            uv_run(loop, UV_RUN_NOWAIT);
            loops++;
        }

        printf("[测试] Waiting for batch %d responses...\n", batch + 1);
        fflush(stdout);

        /* 等待所有async完成 */
        for (int i = 0; i < current_batch_size; i++) {
            const uvrpc_async_result_t* result = uvrpc_async_await_timeout(asyncs[i], 2000);
            if (result && result->status == UVRPC_OK) {
                succeeded++;

                struct timespec req_end;
                clock_gettime(CLOCK_MONOTONIC, &req_end);

                double latency_ms = (req_end.tv_sec - req_starts[i].tv_sec) * 1000.0 +
                                   (req_end.tv_nsec - req_starts[i].tv_nsec) / 1000000.0;
                latency_stats_record(&stats, latency_ms);
            } else {
                printf("  Request %d failed or timeout\n", batch * batch_size + i);
            }
            uvrpc_async_free(asyncs[i]);
        }

        free(asyncs);
        free(req_starts);
        
        printf("[测试] Batch %d complete\n", batch + 1);
        fflush(stdout);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;

    test_result_t result;
    result.total_time_ms = elapsed_ms;
    result.throughput_ops_per_sec = (num_requests / elapsed_ms) * 1000.0;
    result.avg_latency_ms = stats.sum / stats.count;
    result.min_latency_ms = stats.min;
    result.max_latency_ms = stats.max;
    result.p50_latency_ms = calculate_percentile(stats.latencies, stats.count, 50);
    result.p95_latency_ms = calculate_percentile(stats.latencies, stats.count, 95);
    result.p99_latency_ms = calculate_percentile(stats.latencies, stats.count, 99);
    result.success_count = succeeded;
    result.failed_count = num_requests - succeeded;

    printf("\n========== 测试结果 ==========\n");
    printf("总请求数:     %d\n", num_requests);
    printf("成功:         %d (%.2f%%)\n", result.success_count,
           (result.success_count * 100.0) / num_requests);
    printf("失败:         %d\n", result.failed_count);
    printf("总耗时:       %.2f ms\n", result.total_time_ms);
    printf("吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("----------------------------------\n");
    printf("延迟统计:\n");
    printf("  平均:       %.3f ms\n", result.avg_latency_ms);
    printf("  最小:       %.3f ms\n", result.min_latency_ms);
    printf("  最大:       %.3f ms\n", result.max_latency_ms);
    printf("  P50:        %.3f ms\n", result.p50_latency_ms);
    printf("  P95:        %.3f ms\n", result.p95_latency_ms);
    printf("  P99:        %.3f ms\n", result.p99_latency_ms);
    printf("=========================================\n");

    free(stats.latencies);
    return result;
}

/* 运行特定传输模式的测试 */
static void run_transport_test(const char* transport_name, const char* server_addr,
                               uvrpc_transport_t transport, int num_requests, int concurrency) {
    printf("\n========================================\n");
    printf("  %s 传输模式性能测试\n", transport_name);
    printf("========================================\n");
    printf("服务器地址: %s\n", server_addr);
    printf("========================================\n\n");

    /* 提前创建config，用于错误处理 */
    uvrpc_config_t* config = uvrpc_config_new();

    /* 启动服务器线程 */
    volatile int server_running = 0;
    server_thread_arg_t server_arg;
    server_arg.running = &server_running;
    server_arg.bind_addr = server_addr;
    server_arg.transport = transport;
    server_arg.mode = UVRPC_SERVER_CLIENT;

    pthread_t server_tid;
    if (pthread_create(&server_tid, NULL, server_thread, &server_arg) != 0) {
        fprintf(stderr, "Failed to create server thread\n");
        uvrpc_config_free(config);
        return;
    }

    /* 等待服务器启动 */
    printf("[Client] Waiting for server to start...\n");
    int wait_count = 0;
    while (!server_running && wait_count < 100) {
        usleep(10000);
        wait_count++;
        printf("[Client] Waiting... (%d)\n", wait_count);
    }
    if (!server_running) {
        fprintf(stderr, "[Client] Server failed to start\n");
        uvrpc_config_free(config);
        pthread_join(server_tid, NULL);
        return;
    }
    printf("[Client] Server started successfully\n");
    fflush(stdout);
    usleep(200000);

    /* 创建客户端 */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* 创建 ZMQ context for client */
    void* zmq_ctx = zmq_ctx_new();

    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, server_addr);
    uvrpc_config_set_transport(config, transport);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);
    uvrpc_config_set_hwm(config, 10000, 10000);
    uvrpc_config_set_perf_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);

    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        server_running = 0;
        pthread_join(server_tid, NULL);
        uvrpc_config_free(config);
        return;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[Client] Failed to connect to server\n");
        uvrpc_client_free(client);
        server_running = 0;
        pthread_join(server_tid, NULL);
        uvrpc_config_free(config);
        zmq_ctx_term(zmq_ctx);
        return;
    }

    printf("[Client] Connected successfully\n\n");
    fflush(stdout);

    /* 运行事件循环让连接建立 */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    printf("[Client] Running warmup...\n");
    
    /* 预热 */
    warmup(client, &loop);

    printf("[Client] Starting test...\n");
    
    /* 运行测试 */
    test_result_t result = test_generated_api(client, &loop, num_requests, concurrency);
    
    /* 运行事件循环清理 */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);

    /* 停止服务器 */
    server_running = 0;
    pthread_join(server_tid, NULL);
    
    /* Clean up zmq context */
    zmq_ctx_term(zmq_ctx);
    
    /* 打印总结 */
    printf("\n========== %s 总结 ==========\n", transport_name);
    printf("吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("平均延迟:     %.3f ms\n", result.avg_latency_ms);
    printf("P99延迟:      %.3f ms\n", result.p99_latency_ms);
    printf("成功率:       %.2f%%\n",
           (result.success_count * 100.0) / num_requests);
    printf("========================================\n");
    
    /* 等待资源清理 */
    usleep(500000);
}

int main(int argc, char** argv) {
    int num_requests = (argc > 1) ? atoi(argv[1]) : DEFAULT_NUM_REQUESTS;
    int concurrency = (argc > 2) ? atoi(argv[2]) : DEFAULT_CONCURRENCY;

    printf("========================================\n");
    printf("  UVRPC Generated Code Benchmark\n");
    printf("========================================\n");
    printf("总请求数:      %d\n", num_requests);
    printf("并发数:        %d\n", concurrency);
    printf("========================================\n");

    /* 测试 TCP 传输 */
    run_transport_test("TCP", "tcp://127.0.0.1:5555",
                       UVRPC_TRANSPORT_TCP, num_requests, concurrency);

    /* 测试 INPROC 传输 - 使用共享 loop 模式 */
    run_inproc_shared_loop_test(num_requests, concurrency);

    /* 测试 IPC 传输 */
    run_transport_test("IPC", "ipc:///tmp/uvrpc_gen_test.ipc",
                       UVRPC_TRANSPORT_IPC, num_requests, concurrency);

    printf("\n========================================\n");
    printf("  所有测试完成\n");
    printf("========================================\n");

    return 0;
}

/* INPROC 传输测试 - server 和 client 共享同一个 loop */
static void run_inproc_shared_loop_test(int num_requests, int concurrency) {
    printf("\n========================================\n");
    printf("  INPROC 传输模式性能测试（共享 Loop 模式）\n");
    printf("========================================\n");
    printf("服务器地址: inproc://uvrpc_gen_test\n");
    printf("模式: Server 和 Client 共享同一个 Loop\n");
    printf("========================================\n\n");

    /* 创建单一共享 loop */
    uv_loop_t loop;
    uv_loop_init(&loop);

    /* 创建 ZMQ context */
    void* zmq_ctx = zmq_ctx_new();

    /* 创建服务器配置 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, "inproc://uvrpc_gen_test");
    uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(server_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(server_config, zmq_ctx);
    uvrpc_config_set_hwm(server_config, 10000, 10000);
    uvrpc_config_set_perf_mode(server_config, UVRPC_PERF_HIGH_THROUGHPUT);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    if (!server) {
        fprintf(stderr, "[Server] Failed to create server\n");
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return;
    }

    /* 注册服务 */
    if (uvrpc_server_register_service(server, "EchoService.echo", EchoService_echo_Handler, NULL) != UVRPC_OK) {
        fprintf(stderr, "[Server] Failed to register service\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return;
    }

    /* 启动服务器 */
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[Server] Failed to start server\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return;
    }

    printf("[Server] Started on inproc://uvrpc_gen_test\n");
    fflush(stdout);

    /* 运行几次事件循环让服务器完全启动 */
    for (int i = 0; i < 10; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* 创建客户端配置（使用相同的 loop 和 zmq_ctx） */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, "inproc://uvrpc_gen_test");
    uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
    uvrpc_config_set_mode(client_config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(client_config, zmq_ctx);
    uvrpc_config_set_hwm(client_config, 10000, 10000);
    uvrpc_config_set_perf_mode(client_config, UVRPC_PERF_HIGH_THROUGHPUT);

    uvrpc_client_t* client = uvrpc_client_create(client_config);
    if (!client) {
        fprintf(stderr, "[Client] Failed to create client\n");
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return;
    }

    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[Client] Failed to connect to server\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        uvrpc_config_free(server_config);
        uvrpc_config_free(client_config);
        zmq_ctx_term(zmq_ctx);
        uv_loop_close(&loop);
        return;
    }

    printf("[Client] Connected successfully\n\n");
    fflush(stdout);

    /* 运行事件循环让连接建立 */
    for (int i = 0; i < 100; i++) {
        uv_run(&loop, UV_RUN_NOWAIT);
    }

    printf("[Client] Running warmup...\n");
    
    /* 预热 */
    warmup(client, &loop);

    printf("[Client] Starting test...\n");
    
    /* 运行测试 */
    test_result_t result = test_generated_api(client, &loop, num_requests, concurrency);
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    zmq_ctx_term(zmq_ctx);
    uv_loop_close(&loop);
    
    /* 打印总结 */
    printf("\n========== INPROC 总结 ==========\n");
    printf("吞吐量:       %.2f ops/s\n", result.throughput_ops_per_sec);
    printf("平均延迟:     %.3f ms\n", result.avg_latency_ms);
    printf("P99延迟:      %.3f ms\n", result.p99_latency_ms);
    printf("成功率:       %.2f%%\n",
           (result.success_count * 100.0) / num_requests);
    printf("========================================\n");
}