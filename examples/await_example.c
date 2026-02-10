#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Await 风格异步调用示例
 * 
 * 这个示例展示了如何使用类似 JavaScript await 的语法进行 RPC 调用
 * 使用 C99 兼容的宏实现，无需复杂的回调嵌套
 */

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    
    printf("uvrpc Await 风格示例\n");
    printf("服务器地址: %s\n\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    /* 连接到服务器 */
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    /* 创建异步上下文 */
    uvrpc_async_t* async = uvrpc_async_new(&loop);
    if (!async) {
        fprintf(stderr, "Failed to create async context\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("=== 第一次调用 ===\n");
    
    /* 准备请求1 */
    const char* msg1 = "Hello from await!";
    uvrpc_request_t req1;
    memset(&req1, 0, sizeof(req1));
    req1.request_id = 1;
    req1.service_id = "echo.EchoService";
    req1.method_id = "echo";
    req1.request_data = (uint8_t*)msg1;
    req1.request_data_size = strlen(msg1);
    
    uint8_t* serialized1 = NULL;
    size_t size1 = 0;
    if (uvrpc_serialize_request_msgpack(&req1, &serialized1, &size1) != 0) {
        fprintf(stderr, "Failed to serialize request 1\n");
        goto cleanup;
    }
    
    /* 使用 UVRPC_AWAIT 宏等待结果 */
    uvrpc_async_result_t result1;
    UVRPC_AWAIT(result1, async, client, "echo.EchoService", "echo", serialized1, size1);
    
    if (result1.status == UVRPC_OK) {
        printf("✓ 第一次调用成功，响应大小: %zu bytes\n", result1.response_size);
    } else {
        printf("✗ 第一次调用失败: %s\n", uvrpc_strerror(result1.status));
    }
    
    uvrpc_free_serialized_data(serialized1);
    
    printf("\n=== 第二次调用 ===\n");
    
    /* 准备请求2 */
    const char* msg2 = "Second await call";
    uvrpc_request_t req2;
    memset(&req2, 0, sizeof(req2));
    req2.request_id = 2;
    req2.service_id = "echo.EchoService";
    req2.method_id = "echo";
    req2.request_data = (uint8_t*)msg2;
    req2.request_data_size = strlen(msg2);
    
    uint8_t* serialized2 = NULL;
    size_t size2 = 0;
    if (uvrpc_serialize_request_msgpack(&req2, &serialized2, &size2) != 0) {
        fprintf(stderr, "Failed to serialize request 2\n");
        goto cleanup;
    }
    
    /* 再次使用 await */
    uvrpc_async_result_t result2;
    UVRPC_AWAIT(result2, async, client, "echo.EchoService", "echo", serialized2, size2);
    
    if (result2.status == UVRPC_OK) {
        printf("✓ 第二次调用成功，响应大小: %zu bytes\n", result2.response_size);
    } else {
        printf("✗ 第二次调用失败: %s\n", uvrpc_strerror(result2.status));
    }
    
    uvrpc_free_serialized_data(serialized2);
    
    printf("\n=== 循环调用示例 ===\n");
    
    /* 循环调用多次 */
    for (int i = 0; i < 3; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Loop iteration %d", i);
        
        uvrpc_request_t req;
        memset(&req, 0, sizeof(req));
        req.request_id = 100 + i;
        req.service_id = "echo.EchoService";
        req.method_id = "echo";
        req.request_data = (uint8_t*)msg;
        req.request_data_size = strlen(msg);
        
        uint8_t* serialized = NULL;
        size_t size = 0;
        if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) == 0) {
            uvrpc_async_result_t result;
            UVRPC_AWAIT(result, async, client, "echo.EchoService", "echo", serialized, size);
            
            if (result.status == UVRPC_OK) {
                printf("  [%d] ✓ 成功 (响应: %zu bytes)\n", i, result.response_size);
            } else {
                printf("  [%d] ✗ 失败: %s\n", i, uvrpc_strerror(result.status));
            }
            
            uvrpc_free_serialized_data(serialized);
        }
    }
    
    printf("\n=== 测试完成 ===\n");
    
cleanup:
    uvrpc_async_free(async);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    
    return 0;
}
