#include "../include/uvrpc.h"
#include "../src/uvrpc_internal.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mpack.h>

/* Echo 服务处理器 */
int echo_handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;  /* 未使用 */

    /* 解析请求 (使用 mpack) */
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, (const char*)request_data, request_size);

    uint32_t count = mpack_expect_map_max(&reader, 10);

    char message[1024] = {0};
    int64_t timestamp = 0;

    for (uint32_t i = count; i > 0 && mpack_reader_error(&reader) == mpack_ok; --i) {
        char key[128];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "message") == 0) {
            uint32_t len = mpack_expect_str(&reader);
            if (mpack_reader_error(&reader) == mpack_ok && len > 0 && len < sizeof(message)) {
                const char* str = mpack_read_bytes_inplace(&reader, len);
                if (mpack_reader_error(&reader) == mpack_ok && str) {
                    memcpy(message, str, len);
                    message[len] = '\0';
                }
                mpack_done_str(&reader);
            } else {
                mpack_discard(&reader);
            }
        } else if (strcmp(key, "timestamp") == 0) {
            timestamp = (int64_t)mpack_expect_int(&reader);
        } else {
            mpack_discard(&reader);
        }
    }

    mpack_done_map(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        fprintf(stderr, "Failed to parse EchoRequest\n");
        return UVRPC_ERROR;
    }

    mpack_reader_destroy(&reader);

    printf("[Echo Server] Received: %s (timestamp: %ld)\n", message, (long)timestamp);

    /* 构造响应 (使用 mpack) */
    char buffer[4096];
    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer, sizeof(buffer));

    mpack_start_map(&writer, 2);

    /* reply 字段 */
    mpack_write_cstr(&writer, "reply");
    char reply[2048];
    snprintf(reply, sizeof(reply), "Echo: %s", message);
    mpack_write_cstr(&writer, reply);

    /* processed_at 字段 */
    mpack_write_cstr(&writer, "processed_at");
    mpack_write_int(&writer, (int64_t)time(NULL));

    mpack_finish_map(&writer);

    if (mpack_writer_error(&writer) != mpack_ok) {
        fprintf(stderr, "Failed to build EchoResponse\n");
        return UVRPC_ERROR;
    }

    size_t size = mpack_writer_buffer_used(&writer);
    *response_data = (uint8_t*)UVRPC_MALLOC(size);
    if (!*response_data) {
        fprintf(stderr, "Failed to allocate response data\n");
        return UVRPC_ERROR_NO_MEMORY;
    }

    memcpy(*response_data, buffer, size);
    *response_size = size;

    printf("[Echo Server] Sent: %s\n", reply);

    return UVRPC_OK;
}

int main(int argc, char** argv) {
    const char* bind_addr = (argc > 1) ? argv[1] : "tcp://*:5555";
    int mode_arg = (argc > 2) ? atoi(argv[2]) : 0;
    uvrpc_mode_t mode = (uvrpc_mode_t)mode_arg;

    printf("Starting Echo Server on %s (mode: %s)\n", bind_addr, uvrpc_mode_name(mode));

    /* 创建 libuv 事件循环 */
    uv_loop_t* loop = uv_default_loop();

    /* 创建 RPC 服务器（使用模式枚举） */
    uvrpc_server_t* server = uvrpc_server_new(loop, bind_addr, mode);
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