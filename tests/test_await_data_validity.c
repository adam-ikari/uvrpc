#include "../include/uvrpc.h"
#include "../src/msgpack_wrapper.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* 测试服务处理器 - 返回特定的测试数据 */
int test_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;
    (void)request_data;
    (void)request_size;
    
    /* 返回特定的测试数据以便验证 */
    const char* test_response = "TEST_DATA_VALIDITY_CHECK";
    size_t len = strlen(test_response);
    
    *response_data = (uint8_t*)UVRPC_MALLOC(len);
    if (!*response_data) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memcpy(*response_data, test_response, len);
    *response_size = len;
    
    return UVRPC_OK;
}

/* 服务器线程函数 */
void* server_thread(void* arg) {
    (void)arg;
    const char* server_addr = "tcp://127.0.0.1:5557";
    
    printf("[SERVER] Starting on %s...\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = uvrpc_server_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!server) {
        fprintf(stderr, "[SERVER] Failed to create server\n");
        return NULL;
    }
    
    uvrpc_server_register_service(server, "test.TestService", test_handler, NULL);
    
    if (uvrpc_server_start(server) != UVRPC_OK) {
        fprintf(stderr, "[SERVER] Failed to start server\n");
        uvrpc_server_free(server);
        return NULL;
    }
    
    printf("[SERVER] Running...\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uv_loop_close(&loop);
    
    printf("[SERVER] Stopped\n");
    return NULL;
}

/* 客户端测试 */
int client_test() {
    const char* server_addr = "tcp://127.0.0.1:5557";
    
    printf("[CLIENT] Connecting to %s...\n", server_addr);
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_REQ_REP);
    if (!client) {
        fprintf(stderr, "[CLIENT] Failed to create client\n");
        return 1;
    }
    
    if (uvrpc_client_connect(client) != UVRPC_OK) {
        fprintf(stderr, "[CLIENT] Failed to connect\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    printf("[CLIENT] Connected\n");
    
    /* 创建 async 上下文 */
    uvrpc_async_t* async = uvrpc_async_new(&loop);
    if (!async) {
        fprintf(stderr, "[CLIENT] Failed to create async context\n");
        uvrpc_client_free(client);
        return 1;
    }
    
    /* 准备请求 */
    const char* msg = "test";
    uvrpc_request_t req;
    memset(&req, 0, sizeof(req));
    req.request_id = 1;
    req.service_id = "test.TestService";
    req.method_id = "test";
    req.request_data = (uint8_t*)msg;
    req.request_data_size = strlen(msg);
    
    uint8_t* serialized = NULL;
    size_t size = 0;
    if (uvrpc_serialize_request_msgpack(&req, &serialized, &size) != 0) {
        fprintf(stderr, "[CLIENT] Failed to serialize request\n");
        uvrpc_async_free(async);
        uvrpc_client_free(client);
        return 1;
    }
    
    int success_count = 0;
    int total_count = 3;
    
    /* 测试 1: 第一次 await 调用 */
    printf("\n[CLIENT] Test 1: First await call...\n");
    uvrpc_async_result_t result1;
    UVRPC_AWAIT(result1, async, client, "test.TestService", "test", serialized, size);
    
    if (result1.status == UVRPC_OK) {
        const char* expected = "TEST_DATA_VALIDITY_CHECK";
        if (result1.response_size == strlen(expected) &&
            memcmp(result1.response_data, expected, result1.response_size) == 0) {
            printf("  [CLIENT] ✓ Data valid! Response: '%.*s'\n", (int)result1.response_size,
                   (const char*)result1.response_data);
            success_count++;
        } else {
            printf("  [CLIENT] ✗ Data invalid! Expected: '%s', Got: %zu bytes\n", expected, result1.response_size);
        }
    } else {
        printf("  [CLIENT] ✗ Call failed: %s\n", uvrpc_strerror(result1.status));
    }
    
    /* 测试 2: 第二次 await 调用（复用 async 上下文） */
    printf("\n[CLIENT] Test 2: Second await call (reusing async context)...\n");
    uvrpc_async_result_t result2;
    UVRPC_AWAIT(result2, async, client, "test.TestService", "test", serialized, size);
    
    if (result2.status == UVRPC_OK) {
        const char* expected = "TEST_DATA_VALIDITY_CHECK";
        if (result2.response_size == strlen(expected) &&
            memcmp(result2.response_data, expected, result2.response_size) == 0) {
            printf("  [CLIENT] ✓ Data valid! Response: '%.*s'\n", (int)result2.response_size,
                   (const char*)result2.response_data);
            success_count++;
        } else {
            printf("  [CLIENT] ✗ Data invalid! Expected: '%s', Got: %zu bytes\n", expected, result2.response_size);
        }
    } else {
        printf("  [CLIENT] ✗ Call failed: %s\n", uvrpc_strerror(result2.status));
    }
    
    /* 测试 3: 第三次 await 调用 */
    printf("\n[CLIENT] Test 3: Third await call...\n");
    uvrpc_async_result_t result3;
    UVRPC_AWAIT(result3, async, client, "test.TestService", "test", serialized, size);
    
    if (result3.status == UVRPC_OK) {
        const char* expected = "TEST_DATA_VALIDITY_CHECK";
        if (result3.response_size == strlen(expected) &&
            memcmp(result3.response_data, expected, result3.response_size) == 0) {
            printf("  [CLIENT] ✓ Data valid! Response: '%.*s'\n", (int)result3.response_size,
                   (const char*)result3.response_data);
            success_count++;
        } else {
            printf("  [CLIENT] ✗ Data invalid! Expected: '%s', Got: %zu bytes\n", expected, result3.response_size);
        }
    } else {
        printf("  [CLIENT] ✗ Call failed: %s\n", uvrpc_strerror(result3.status));
    }
    
    uvrpc_free_serialized_data(serialized);
    
    /* 清理 */
    uvrpc_async_free(async);
    uvrpc_client_disconnect(client);
    uvrpc_client_free(client);
    uv_loop_close(&loop);
    
    printf("\n[CLIENT] Tests completed: %d/%d passed\n", success_count, total_count);
    
    return (success_count == total_count) ? 0 : 1;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  Await Data Validity Test\n");
    printf("========================================\n\n");
    
    /* 启动服务器线程 */
    pthread_t server_tid;
    pthread_create(&server_tid, NULL, server_thread, NULL);
    
    /* 等待服务器启动 */
    sleep(1);
    
    /* 运行客户端测试 */
    int result = client_test();
    
    /* 测试结果 */
    printf("\n========================================\n");
    if (result == 0) {
        printf("  ALL TESTS PASSED ✓\n");
    } else {
        printf("  SOME TESTS FAILED ✗\n");
    }
    printf("========================================\n");
    
    return result;
}