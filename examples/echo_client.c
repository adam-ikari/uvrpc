#include "../include/uvrpc.h"
#include "echo_generated.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 响应回调函数 */
void echo_response_callback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    const char* request_message = (const char*)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "RPC call failed: %s\n", uvrpc_strerror(status));
        return;
    }

    /* 解析响应 */
    flatbuffers_t* fb = flatbuffers_init(response_data, response_size);
    if (!fb) {
        fprintf(stderr, "Failed to init flatbuffers\n");
        return;
    }

    echo_EchoResponse_table_t response;
    if (!echo_EchoResponse_as_root(fb)) {
        fprintf(stderr, "Invalid EchoResponse message\n");
        flatbuffers_clear(fb);
        return;
    }
    echo_EchoResponse_init(&response, fb);

    /* 获取响应数据 */
    const char* reply = echo_EchoResponse_reply(&response);
    int64_t processed_at = echo_EchoResponse_processed_at(&response);

    printf("[Echo Client] Request: %s\n", request_message);
    printf("[Echo Client] Response: %s (processed at: %ld)\n", reply, (long)processed_at);

    flatbuffers_clear(fb);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    const char* message = (argc > 2) ? argv[2] : "Hello, uvrpc!";

    printf("Starting Echo Client connecting to %s\n", server_addr);

    /* 创建 libuv 事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 RPC 客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, server_addr);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    printf("Echo Client connected to server\n");

    /* 构造请求 */
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        fprintf(stderr, "Failed to create flatbuffers builder\n");
        uvrpc_client_free(client);
        return 1;
    }

    /* 创建 message 字符串 */
    flatbuffers_string_ref_t message_ref = flatbuffers_string_create(builder, message);

    /* 获取当前时间戳 */
    int64_t timestamp = time(NULL);

    /* 创建 EchoRequest */
    echo_EchoRequest_start(builder);
    echo_EchoRequest_message_add(builder, message_ref);
    echo_EchoRequest_timestamp_add(builder, timestamp);
    flatbuffers_uint8_vec_ref_t request_ref = echo_EchoRequest_end(builder);

    echo_EchoRequest_create_as_root(builder, request_ref);

    /* 获取序列化数据 */
    size_t request_size;
    const uint8_t* request_data = flatbuffers_builder_get_data(builder, &request_size);

    /* 调用 Echo 服务 */
    int rc = uvrpc_client_call(client, "echo.EchoService", request_data, request_size,
                               echo_response_callback, (void*)message);
    if (rc != UVRPC_OK) {
        fprintf(stderr, "Failed to call echo service: %s\n", uvrpc_strerror(rc));
        flatbuffers_builder_clear(builder);
        uvrpc_client_free(client);
        return 1;
    }

    flatbuffers_builder_clear(builder);

    printf("Echo Client sent request, waiting for response...\n");

    /* 运行事件循环（处理响应） */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_client_free(client);
    uv_loop_close(loop);

    printf("Echo Client stopped\n");

    return 0;
}