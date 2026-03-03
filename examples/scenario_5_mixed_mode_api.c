/**
 * Scenario 5: 混合模式 (Mixed Mode)
 * 
 * 适用场景：
 * - 需要多种 RPC 模式的复杂应用
 * - RESTful API 风格的服务
 * - 微服务架构
 * 
 * 特点：
 * - 同时使用请求-响应、流式、单向、广播
 * - 根据业务需求选择合适的模式
 * - 灵活的架构设计
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 处理 1: 用户查询（请求-响应） */
static void get_user_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    const char* user_id = (const char*)req->params;
    char user_info[256];
    snprintf(user_info, sizeof(user_info), "User: %s, Role: Admin, Active: yes", user_id);
    
    printf("[API] GET /user/%s\n", user_id);
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)user_info, strlen(user_info) + 1);
}

/* 处理 2: 日志记录（单向） */
static void log_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size > 0) {
        printf("[API] POST /log: %s\n", (char*)req->params);
        // oneway：不发送响应
    }
}

/* 处理 3: 数据流式下载（流式） */
static void download_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    printf("[API] GET /download (streaming)\n");
    
    /* 模拟 5MB 文件，分 50 块发送 */
    int total_chunks = 50;
    for (int i = 0; i < total_chunks; i++) {
        char chunk[128];
        snprintf(chunk, sizeof(chunk), "Data chunk %d of %d (100KB each)", i + 1, total_chunks);
        
        if (i < total_chunks - 1) {
            uvrpc_request_send_response_more(req, (uint8_t*)chunk, strlen(chunk) + 1);
        } else {
            uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)chunk, strlen(chunk) + 1);
        }
        
        usleep(5000);  // 5ms 延迟
    }
    
    printf("[API] Download complete: 50 chunks sent\n");
}

/* 处理 4: 系统通知（广播） */
static void notify_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size > 0) {
        char* notification = (char*)req->params;
        printf("[API] POST /notify: %s\n", notification);
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)notification, strlen(notification) + 1);
    }
}

/* 客户端上下文 */
typedef struct {
    int test_count;
    uv_loop_t* loop;
} client_context_t;

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

/* 测试回调 */
static void test_callback(uvrpc_response_t* resp, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    context->test_count++;
    
    if (resp->status == UVRPC_OK) {
        printf("[CLIENT] Test %d completed\n", context->test_count);
fflush(stdout);
    }
    
    /* 所有测试完成后停止 */
    if (context->test_count >= 4) {
        uv_stop(context->loop);
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5560";
    
    printf("=== Scenario 5: Mixed Mode API ===\n");
    printf("Pattern: Combining all RPC modes\n");
    printf("Use case: RESTful API, microservices\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建 API 服务器 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "GET:/user", get_user_handler, NULL);
    uvrpc_server_register(server, "POST:/log", log_handler, NULL);
    uvrpc_server_register(server, "GET:/download", download_handler, NULL);
    uvrpc_server_register(server, "POST:/notify", notify_handler, NULL);
    uvrpc_server_start(server);
    
    printf("[SETUP] API server started\n\n");
fflush(stdout);
    
    /* 创建客户端 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    
    /* 客户端上下文 */
    client_context_t ctx = {0, &loop};
    
    uvrpc_client_connect_with_callback(client, on_connect, NULL);
    printf("[SETUP] Client connected\n\n");
    
    /* 测试 1: 用户查询（请求-响应） */
    printf("[TEST 1] User query (Request-Response)\n");
    const char* user_id = "user123";
    uvrpc_client_call(client, "GET:/user", (uint8_t*)user_id, strlen(user_id) + 1,
                     test_callback, &ctx);
    uv_run(&loop, UV_RUN_DEFAULT);
    sleep(1);
    
    /* 测试 2: 日志记录（单向） */
    printf("\n[TEST 2] Log submission (Oneway)\n");
    const char* log_msg = "User performed action at timestamp 1234567890";
    uvrpc_client_call(client, "POST:/log", (uint8_t*)log_msg, strlen(log_msg) + 1,
                     test_callback, &ctx);
    uv_run(&loop, UV_RUN_DEFAULT);
    sleep(1);
    
    /* 测试 3: 文件下载（流式） */
    printf("\n[TEST 3] File download (Streaming)\n");
    const char* file_path = "/path/to/large_file.zip";
    uvrpc_client_call(client, "GET:/download", (uint8_t*)file_path, strlen(file_path) + 1,
                     test_callback, &ctx);
    uv_run(&loop, UV_RUN_DEFAULT);
    sleep(1);
    
    /* 测试 4: 系统通知（广播） */
    printf("\n[TEST 4] System notification (Broadcast)\n");
    const char* notification = "System maintenance scheduled at 02:00 UTC";
    uvrpc_client_call(client, "POST:/notify", (uint8_t*)notification, strlen(notification) + 1,
                     test_callback, &ctx);
    uv_run(&loop, UV_RUN_DEFAULT);
    sleep(1);
    
    /* 运行事件循环，所有测试完成后通过 uv_stop 退出 */
    int iterations = 0;
    while (ctx.test_count < 4 && iterations < 100) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_config_free(client_config);
    uvrpc_server_free(server);
    uvrpc_config_free(server_config);
    uv_loop_close(&loop);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}
