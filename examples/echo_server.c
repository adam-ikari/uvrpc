#include "../include/uvrpc.h"
#include "echo_generated.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Echo 服务处理器 */
int echo_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;  /* 未使用 */

    /* 解析请求 */
    flatbuffers_t* fb = flatbuffers_init(request_data, request_size);
    if (!fb) {
        fprintf(stderr, "Failed to init flatbuffers\n");
        return UVRPC_ERROR;
    }

    echo_EchoRequest_table_t request;
    if (!echo_EchoRequest_as_root(fb)) {
        fprintf(stderr, "Invalid EchoRequest message\n");
        flatbuffers_clear(fb);
        return UVRPC_ERROR;
    }
    echo_EchoRequest_init(&request, fb);

    /* 获取消息 */
    const char* message = echo_EchoRequest_message(&request);
    int64_t timestamp = echo_EchoRequest_timestamp(&request);

    printf("[Echo Server] Received: %s (timestamp: %ld)\n", message, (long)timestamp);

    /* 构造响应 */
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        fprintf(stderr, "Failed to create flatbuffers builder\n");
        flatbuffers_clear(fb);
        return UVRPC_ERROR;
    }

    /* 创建 reply 字符串 */
    char reply[256];
    snprintf(reply, sizeof(reply), "Echo: %s", message);
    flatbuffers_string_ref_t reply_ref = flatbuffers_string_create(builder, reply);

    /* 获取当前时间戳 */
    int64_t processed_at = time(NULL);

    /* 创建 EchoResponse */
    echo_EchoResponse_start(builder);
    echo_EchoResponse_reply_add(builder, reply_ref);
    echo_EchoResponse_processed_at_add(builder, processed_at);
    flatbuffers_uint8_vec_ref_t response_ref = echo_EchoResponse_end(builder);

    echo_EchoResponse_create_as_root(builder, response_ref);

    /* 获取序列化数据 */
    const uint8_t* data = flatbuffers_builder_get_data(builder, response_size);
    *response_data = (uint8_t*)UVRPC_MALLOC(*response_size);
    if (!*response_data) {
        fprintf(stderr, "Failed to allocate response data\n");
        flatbuffers_builder_clear(builder);
        flatbuffers_clear(fb);
        return UVRPC_ERROR_NO_MEMORY;
    }

    memcpy(*response_data, data, *response_size);

    flatbuffers_builder_clear(builder);
    flatbuffers_clear(fb);

    printf("[Echo Server] Sent: %s\n", reply);

    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "tcp://*:5555";

    printf("Starting Echo Server on %s\n", bind_addr);

    /* 创建 libuv 事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 RPC 服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, bind_addr);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }

    /* 注册 Echo 服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_handler, NULL);
    if (rc != UVRPC_OK) {
        fprintf(stderr, "Failed to register echo service: %s\n", uvrpc_strerror(rc));
        uvrpc_server_free(server);
        return 1;
    }

    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    if (rc != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %s\n", uvrpc_strerror(rc));
        uvrpc_server_free(server);
        return 1;
    }

    printf("Echo Server is running...\n");
    printf("Press Ctrl+C to stop\n");

    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_server_free(server);
    uv_loop_close(loop);

    printf("Echo Server stopped\n");

    return 0;
}