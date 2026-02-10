#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * uvrpc 完整示例
 * 
 * 展示 uvrpc 的核心功能：
 * 1. 服务端注册服务
 * 2. 客户端同步调用（await 风格）
 * 3. 客户端异步调用（回调风格）
 * 4. 错误处理
 * 5. 资源清理
 */

/* ==================== 服务端处理器 ==================== */

/* 简单的 Echo 服务处理器 */
int echo_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;
    
    /* 直接返回请求数据作为响应 */
    *response_data = (uint8_t*)UVRPC_MALLOC(request_size);
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memcpy(*response_data, request_data, request_size);
    *response_size = request_size;
    
    return UVRPC_OK;
}

/* 简单的计算服务处理器 */
int calculate_handler(void* ctx,
                      const uint8_t* request_data,
                      size_t request_size,
                      uint8_t** response_data,
                      size_t* response_size) {
    (void)ctx;
    
    /* 反序列化请求 */
    uvrpc_request_t request;
    if (uvrpc_deserialize_request_msgpack(request_data, request_size, &request) != 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* 假设请求数据是一个数字（简化示例） */
    int value = 0;
    if (request.request_data && request.request_data_size >= sizeof(int)) {
        memcpy(&value, request.request_data, sizeof(int));
    }
    
    /* 计算平方 */
    int result = value * value;
    
    /* 构建响应 */
    *response_data = (uint8_t*)UVRPC_MALLOC(sizeof(int));
    if (!*response_data) {
        uvrpc_free_request(&request);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memcpy(*response_data, &result, sizeof(int));
    *response_size = sizeof(int);
    
    uvrpc_free_request(&request);
    return UVRPC_OK;
}

/* ==================== 客户端回调 ==================== */

typedef struct {
    int call_count;
    int success_count;
    int total_result;
} async_context_t;

void async_callback(void* ctx, int status,
                    const uint8_t* response_data,
                    size_t response_size) {
    async_context_t* async_ctx = (async_context_t*)ctx;
    async_ctx->call_count++;
    
    if (status == UVRPC_OK && response_size == sizeof(int)) {
        int result;
        memcpy(&result, response_data, sizeof(int));
        async_ctx->success_count++;
        async_ctx->total_result += result;
        printf("[异步] 调用 #%d 成功，结果: %d\n",
               async_ctx->call_count, result);
    } else {
        printf("[异步] 调用 #%d 失败: %s\n",
               async_ctx->call_count, uvrpc_strerror(status));
    }
}

/* ==================== 主程序 ==================== */

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    printf("========================================\n");
    printf("uvrpc 完整示例\n");
    printf("========================================\n");
    printf("服务器地址: %s\n\n", server_addr);
    
    /* 创建事件循环 */
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* ==================== 1. 创建并启动服务端 ==================== */
    
    printf("[1] 创建服务端...\n");
    uvrpc_server_t* server = uvrpc_server_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!server) {
        fprintf(stderr, "创建服务端失败\n");
        return 1;
    }
    
    /* 注册服务 */
    printf("[2] 注册服务...\n");
    uvrpc_server_register_service(server, "echo.EchoService", echo_handler, NULL);
    uvrpc_server_register_service(server, "calc.CalculateService", calculate_handler, NULL);
    
    /* 启动服务端 */
    printf("[3] 启动服务端...\n");
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "启动服务端失败\n");
        uvrpc_server_free(server);
        return 1;
    }
    
    printf("✓ 服务端已启动\n\n");
    
    /* 在后台运行服务端（模拟多线程环境） */
    /* 实际应用中，服务端通常运行在单独的线程中 */
    
    /* ==================== 2. 创建客户端 ==================== */
    
    printf("[4] 创建客户端...\n");
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "创建客户端失败\n");
        uvrpc_server_free(server);
        return 1;
    }
    
    /* 连接到服务器 */
    printf("[5] 连接到服务器...\n");
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "连接服务器失败\n");
        uvrpc_client_free(client);
        uvrpc_server_free(server);
        return 1;
    }
    
    printf("✓ 客户端已连接\n\n");
    
    /* ==================== 3. 同步调用（Await 风格） ==================== */
    
    printf("========================================\n");
    printf("同步调用（Await 风格）\n");
    printf("========================================\n");
    
    uvrpc_async_t* async = uvrpc_async_new(&loop);
    if (!async) {
        fprintf(stderr, "创建 async 上下文失败\n");
        goto cleanup;
    }
    
    /* Echo 服务调用 */
    printf("\n调用 Echo 服务...\n");
    const char* echo_msg = "Hello from uvrpc!";
    uvrpc_request_t echo_req;
    memset(&echo_req, 0, sizeof(echo_req));
    echo_req.request_id = 1;
    echo_req.service_id = "echo.EchoService";
    echo_req.method_id = "echo";
    echo_req.request_data = (uint8_t*)echo_msg;
    echo_req.request_data_size = strlen(echo_msg);
    
    uint8_t* echo_serialized = NULL;
    size_t echo_size = 0;
    if (uvrpc_serialize_request_msgpack(&echo_req, &echo_serialized, &echo_size) == 0) {
        uvrpc_async_result_t echo_result;
        UVRPC_AWAIT(echo_result, async, client, "echo.EchoService", "echo",
                     echo_serialized, echo_size);
        
        if (echo_result.status == UVRPC_OK) {
            printf("✓ Echo 成功，响应大小: %zu bytes\n", echo_result.response_size);
        } else {
            printf("✗ Echo 失败: %s\n", uvrpc_strerror(echo_result.status));
        }
        
        uvrpc_free_serialized_data(echo_serialized);
    }
    
    /* 计算服务调用 */
    printf("\n调用计算服务...\n");
    int value = 10;
    uvrpc_request_t calc_req;
    memset(&calc_req, 0, sizeof(calc_req));
    calc_req.request_id = 2;
    calc_req.service_id = "calc.CalculateService";
    calc_req.method_id = "square";
    calc_req.request_data = (uint8_t*)&value;
    calc_req.request_data_size = sizeof(int);
    
    uint8_t* calc_serialized = NULL;
    size_t calc_size = 0;
    if (uvrpc_serialize_request_msgpack(&calc_req, &calc_serialized, &calc_size) == 0) {
        uvrpc_async_result_t calc_result;
        UVRPC_AWAIT(calc_result, async, client, "calc.CalculateService", "square",
                     calc_serialized, calc_size);
        
        if (calc_result.status == UVRPC_OK && calc_result.response_size == sizeof(int)) {
            int result;
            memcpy(&result, calc_result.response_data, sizeof(int));
            printf("✓ 计算成功: %d 的平方 = %d\n", value, result);
        } else {
            printf("✗ 计算失败: %s\n", uvrpc_strerror(calc_result.status));
        }
        
        uvrpc_free_serialized_data(calc_serialized);
    }
    
    /* ==================== 4. 异步调用（回调风格） ==================== */
    
    printf("\n========================================\n");
    printf("异步调用（回调风格）\n");
    printf("========================================\n\n");
    
    async_context_t async_ctx = {0, 0, 0};
    
    /* 发起多个异步调用 */
    for (int i = 1; i <= 3; i++) {
        int num = i * 5;
        
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = 100 + i;
        req.service_id = "calc.CalculateService";
        req.method_id = "square";
        req.request_data = (uint8_t*)&num;
        req.request_data_size = sizeof(int);
        
        uint8_t* serialized = NULL;
        size_t size = 0;
        if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) == 0) {
            if (uvrpc_client_call(client, "calc.CalculateService", "square",
                                   serialized, size,
                                   async_callback, &async_ctx) == UVRPC_OK) {
                printf("发起异步调用 #%d: %d 的平方\n", i, num);
            }
            
            uvrpc_free_serialized_data(serialized);
        }
        
        /* 等待本次调用完成 */
        uv_run(&loop, UV_RUN_NOWAIT);
        uv_run(&loop, UV_RUN_NOWAIT);
        usleep(10000);
    }
    
    printf("\n异步调用完成: %d/%d 成功，总和: %d\n\n",
           async_ctx.success_count, async_ctx.call_count, async_ctx.total_result);
    
    /* ==================== 5. 清理资源 ==================== */
    
cleanup:
    printf("========================================\n");
    printf("清理资源\n");
    printf("========================================\n");
    
    if (async) {
        uvrpc_async_free(async);
    }
    
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    
    printf("✓ 资源已清理\n");
    printf("========================================\n");
    printf("示例运行完成\n");
    printf("========================================\n");
    
    return 0;
}