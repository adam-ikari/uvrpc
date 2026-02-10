#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * 回调方式 vs Await 方式对比
 */

/* ==================== 回调方式（传统） ==================== */

typedef struct {
    int call_count;
    int success_count;
} callback_context_t;

void callback_handler(void* ctx, int status,
                      const uint8_t* response_data,
                      size_t response_size) {
    (void)response_data;
    
    callback_context_t* cb_ctx = (callback_context_t*)ctx;
    cb_ctx->call_count++;
    
    if (status == UVRPC_OK) {
        cb_ctx->success_count++;
        printf("[回调] 调用 #%d 成功，响应: %zu bytes\n",
               cb_ctx->call_count, response_size);
    } else {
        printf("[回调] 调用 #%d 失败: %s\n",
               cb_ctx->call_count, uvrpc_strerror(status));
    }
}

void test_callback_style(uvrpc_client_t* client) {
    printf("\n=== 回调方式（传统） ===\n");
    
    callback_context_t cb_ctx = {0, 0};
    
    /* 准备请求 */
    const char* msg = "Callback test";
    uvrpc_request_t req;
    memset(&req, 0, sizeof(req));
    req.request_id = 1;
    req.service_id = "echo.EchoService";
    req.method_id = "echo";
    req.request_data = (uint8_t*)msg;
    req.request_data_size = strlen(msg);
    
    uint8_t* serialized = NULL;
    size_t size = 0;
    if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) != 0) {
        fprintf(stderr, "序列化失败\n");
        return;
    }
    
    /* 逐次发起调用并等待结果 */
    for (int i = 0; i < 3; i++) {
        int call_success = uvrpc_client_call(client, "echo.EchoService", "echo",
                                               serialized, size,
                                               callback_handler, &cb_ctx);
        if (call_success != UVRPC_OK) {
            fprintf(stderr, "发起调用 #%d 失败\n", i + 1);
            continue;
        }
        
        /* 等待本次调用完成 */
        uv_run(uv_default_loop(), UV_RUN_NOWAIT);
        uv_run(uv_default_loop(), UV_RUN_NOWAIT);
        usleep(10000); /* 等待响应 */
    }
    
    printf("[回调] 总计: %d 次调用，%d 次成功\n\n",
           cb_ctx.call_count, cb_ctx.success_count);
    
    uvrpc_free_serialized_data(serialized);
}

/* ==================== Await 方式（现代） ==================== */

void test_await_style(uvrpc_client_t* client, uvrpc_async_t* async) {
    printf("=== Await 方式（现代） ===\n");
    
    int success_count = 0;
    
    /* 准备请求 */
    const char* msg = "Await test";
    uvrpc_request_t req;
    memset(&req, 0, sizeof(req));
    req.request_id = 1;
    req.service_id = "echo.EchoService";
    req.method_id = "echo";
    req.request_data = (uint8_t*)msg;
    req.request_data_size = strlen(msg);
    
    uint8_t* serialized = NULL;
    size_t size = 0;
    if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) != 0) {
        fprintf(stderr, "序列化失败\n");
        return;
    }
    
    /* 使用 await 发起 3 次调用 */
    for (int i = 0; i < 3; i++) {
        uvrpc_async_result_t result;
        UVRPC_AWAIT(result, async, client, "echo.EchoService", "echo",
                     serialized, size);
        
        if (result.status == UVRPC_OK) {
            success_count++;
            printf("[Await] 调用 #%d 成功，响应: %zu bytes\n",
                   i + 1, result.response_size);
        } else {
            printf("[Await] 调用 #%d 失败: %s\n",
                   i + 1, uvrpc_strerror(result.status));
        }
    }
    
    printf("[Await] 总计: 3 次调用，%d 次成功\n\n", success_count);
    
    uvrpc_free_serialized_data(serialized);
}

/* ==================== 代码复杂度对比 ==================== */

void test_complexity_comparison() {
    printf("代码复杂度对比：\n");
    printf("┌─────────────────┬──────────────┬──────────────┐\n");
    printf("│ 特性            │ 回调方式     │ Await 方式   │\n");
    printf("├─────────────────┼──────────────┼──────────────┤\n");
    printf("│ 代码行数        │ ~30 行       │ ~15 行       │\n");
    printf("│ 回调嵌套层级    │ 需要手动管理 │ 无嵌套       │\n");
    printf("│ 错误处理        │ 分散         │ 集中         │\n");
    printf("│ 可读性          │ 较差         │ 优秀         │\n");
    printf("│ 状态管理        │ 手动         │ 自动         │\n");
    printf("│ 代码维护性      │ 较难         │ 容易         │\n");
    printf("└─────────────────┴──────────────┴──────────────┘\n\n");
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    printf("uvrpc 回调 vs Await 对比测试\n");
    printf("服务器地址: %s\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "创建客户端失败\n");
        return 1;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "连接服务器失败\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    /* 创建 async 上下文 */
    uvrpc_async_t* async = uvrpc_async_new(&loop);
    if (!async) {
        fprintf(stderr, "创建 async 上下文失败\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    /* 显示复杂度对比 */
    test_complexity_comparison();
    
    /* 测试回调方式 */
    test_callback_style(client);
    
    /* 测试 await 方式 */
    test_await_style(client, async);
    
    printf("=== 测试完成 ===\n");
    
    /* 清理 */
    uvrpc_async_free(async);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    
    return 0;
}