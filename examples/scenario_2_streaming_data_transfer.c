/**
 * Scenario 2: 流式数据传输
 * 
 * 适用场景：
 * - 大文件传输（分块发送）
 * - 实时数据流（如日志、传感器数据）
 * - 分页查询结果
 * - 批量数据处理
 * 
 * 特点：
 * - 单个请求对应多个响应
 * - 使用 uvrpc_request_send_response_more() 发送中间响应
 * - 使用 uvrpc_request_send_response() 发送最后响应
 * - 客户端使用 uvrpc_response_is_stream_more() 和 uvrpc_response_is_stream_end() 检测
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHUNK_SIZE 1024

/* 服务器端：模拟大文件传输 */
static void file_transfer_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    printf("[SERVER] Starting file transfer, msgid=%u\n", req->msgid);
    fflush(stdout);
    
    /* 模拟 10KB 文件，分 10 块发送 */
    int total_chunks = 10;
    for (int i = 0; i < total_chunks; i++) {
        char chunk[CHUNK_SIZE];
        snprintf(chunk, sizeof(chunk), "File chunk %d of %d (data...)", i + 1, total_chunks);
        
        if (i < total_chunks - 1) {
            // 发送中间块（type=2，还有更多）
            printf("[SERVER] Sending chunk %d (more)\n", i + 1);
            fflush(stdout);
            uvrpc_request_send_response_more(req, (uint8_t*)chunk, strlen(chunk) + 1);
        } else {
            // 发送最后一块（type=1，结束）
            printf("[SERVER] Sending chunk %d (last)\n", i + 1);
            fflush(stdout);
            uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)chunk, strlen(chunk) + 1);
        }
        
        usleep(10000);  // 10ms 延迟，模拟网络传输
    }
    
    printf("[SERVER] File transfer complete\n");
    fflush(stdout);
}

/* 客户端上下文 */
typedef struct {
    int chunks_received;
    int total_bytes;
    uv_loop_t* loop;
} client_context_t;

/* 客户端回调 */
static void file_callback(uvrpc_response_t* resp, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    
    if (uvrpc_response_is_stream_more(resp)) {
        context->chunks_received++;
        context->total_bytes += resp->result_size;
        printf("[CLIENT] Received chunk %d: %zu bytes\n", context->chunks_received, resp->result_size);
        fflush(stdout);
    } else if (uvrpc_response_is_stream_end(resp)) {
        context->chunks_received++;
        context->total_bytes += resp->result_size;
        printf("[CLIENT] Received final chunk %d: %zu bytes\n", context->chunks_received, resp->result_size);
        printf("[CLIENT] Transfer complete: %d chunks, %d total bytes\n", context->chunks_received, context->total_bytes);
        fflush(stdout);
        uv_stop(context->loop);  /* Stop the loop after stream end */
    }
}

/* 连接回调 */
static void on_connect(int status, void* ctx) {
    (void)ctx;
    if (status == 0) {
        printf("[CLIENT] Connected successfully\n");
        fflush(stdout);
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5557";
    
    printf("=== Scenario 2: Streaming Data Transfer ===\n");
    printf("Pattern: One request -> Multiple responses\n");
    printf("Use case: File transfer, real-time data, pagination\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建服务器 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "FileTransfer", file_transfer_handler, NULL);
    uvrpc_server_start(server);
    
    /* 创建客户端 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, on_connect, NULL);
    
    /* 客户端上下文 */
    client_context_t ctx = {0, 0, &loop};
    
    /* 运行事件循环一小段时间以等待连接建立 */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* 发送文件传输请求 */
    const char* params = "file:large_file.dat";
    printf("[CLIENT] Requesting file transfer...\n");
    fflush(stdout);
    uvrpc_client_call(client, "FileTransfer", (uint8_t*)params, strlen(params) + 1,
                     file_callback, &ctx);
    
    /* 运行事件循环，收到流结束标记后通过 uv_stop 退出 */
    int iterations = 0;
    while (ctx.chunks_received < 10 && iterations < 200) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uvrpc_config_free(client_config);
    uv_loop_close(&loop);
    
    printf("=== Demo Complete ===\n");
    return 0;
}