#include "../include/uvrpc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mpack.h>

/* 响应回调函数 */
void echo_response_callback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    const char* request_message = (const char*)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "RPC call failed: %s\n", uvrpc_strerror(status));
        return;
    }

    /* 解析响应 (使用 mpack) */
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, (const char*)response_data, response_size);

    uint32_t count = mpack_expect_map_max(&reader, 10);

    char reply[1024] = {0};
    int64_t processed_at = 0;

    for (uint32_t i = count; i > 0 && mpack_reader_error(&reader) == mpack_ok; --i) {
        char key[128];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "reply") == 0) {
            uint32_t len = mpack_expect_str(&reader);
            if (mpack_reader_error(&reader) == mpack_ok && len > 0 && len < sizeof(reply)) {
                const char* str = mpack_read_bytes_inplace(&reader, len);
                if (mpack_reader_error(&reader) == mpack_ok && str) {
                    memcpy(reply, str, len);
                    reply[len] = '\0';
                }
                mpack_done_str(&reader);
            } else {
                mpack_discard(&reader);
            }
        } else if (strcmp(key, "processed_at") == 0) {
            processed_at = (int64_t)mpack_expect_int(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    mpack_done_map(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        fprintf(stderr, "Failed to parse EchoResponse\n");
        return;
    }

    mpack_reader_destroy(&reader);

    printf("[Echo Client] Request: %s\n", request_message);
    printf("[Echo Client] Response: %s (processed at: %ld)\n", reply, (long)processed_at);
    fflush(stdout);
}

int main(int argc, char** argv) {
    const char* server_addr = (argc > 1) ? argv[1] : "tcp://127.0.0.1:5555";
    const char* message = (argc > 2) ? argv[2] : "Hello, uvrpc!";
    int mode_arg = (argc > 3) ? atoi(argv[3]) : 0;
    uvrpc_mode_t mode = (uvrpc_mode_t)mode_arg;

    printf("Starting Echo Client connecting to %s (mode: %s)\n", server_addr, uvrpc_mode_name(mode));
    fflush(stdout);

    /* 创建 libuv 事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 RPC 客户端（使用模式枚举） */
    uvrpc_client_t* client = uvrpc_client_new(loop, server_addr, mode);
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

    printf("Echo Client connected to server\n");
    fflush(stdout);

    /* 构造请求 (使用 mpack) */
    char buffer[4096];
    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer, sizeof(buffer));

    mpack_start_map(&writer, 2);

    /* message 字段 */
    mpack_write_cstr(&writer, "message");
    mpack_write_cstr(&writer, message);

    /* timestamp 字段 */
    mpack_write_cstr(&writer, "timestamp");
    mpack_write_int(&writer, (int64_t)time(NULL));

    mpack_finish_map(&writer);

    if (mpack_writer_error(&writer) != mpack_ok) {
        fprintf(stderr, "Failed to build EchoRequest\n");
        uvrpc_client_free(client);
        return 1;
    }

    size_t request_size = mpack_writer_buffer_used(&writer);

    /* 调用 Echo 服务 */
    int rc = uvrpc_client_call(client, "echo.EchoService", "Echo",
                               (const uint8_t*)buffer, request_size,
                               echo_response_callback, (void*)message);
    if (rc != UVRPC_OK) {
        fprintf(stderr, "Failed to call echo service: %s\n", uvrpc_strerror(rc));
        uvrpc_client_free(client);
        return 1;
    }

    printf("Echo Client sent request, waiting for response...\n");
    fflush(stdout);

    /* 运行事件循环（处理响应） */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_client_free(client);
    uv_loop_close(loop);

    printf("Echo Client stopped\n");

    return 0;
}