/**
 * Scenario 1: 简单请求-响应 (Request-Response)
 * 
 * 适用场景：
 * - 查询操作（如获取用户信息）
 * - 计算操作（如加法、减法）
 * - 状态查询（如检查服务状态）
 * 
 * 特点：
 * - 单个请求对应单个响应
 * - 最常用的 RPC 模式
 * - 使用 uvrpc_request_send_response() 发送响应
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>

/* 服务器端：简单加法运算 */
static void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size == 8) {
        int32_t a = *(int32_t*)req->params;
        int32_t b = *(int32_t*)(req->params + 4);
        int32_t result = a + b;
        
        printf("[SERVER] Adding: %d + %d = %d\n", a, b, result);
        fflush(stdout);
        
        // 发送单个响应（type=1，最后一个）
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    }
}

/* 客户端上下文 */
typedef struct {
    volatile int received;
    uv_loop_t* loop;
} client_context_t;

/* 客户端回调 */
static void add_callback(uvrpc_response_t* resp, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    
    if (resp->status == UVRPC_OK && resp->result_size == sizeof(int32_t)) {
        int32_t result = *(int32_t*)resp->result;
        printf("[CLIENT] Result: %d\n", result);
        fflush(stdout);
    }
    
    context->received = 1;
    uv_stop(context->loop);
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
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5556";
    
    printf("=== Scenario 1: Simple Request-Response ===\n");
    printf("Pattern: One request -> One response\n");
    printf("Use case: Query, calculation, status check\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建服务器 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    printf("[MAIN] Server created\n");
    fflush(stdout);
    
    uvrpc_server_register(server, "Add", add_handler, NULL);
    printf("[MAIN] Handler registered\n");
    fflush(stdout);
    
    uvrpc_server_start(server);
    printf("[MAIN] Server started\n");
    fflush(stdout);
    
    /* 创建客户端 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    uvrpc_client_connect_with_callback(client, on_connect, NULL);
    
    /* 客户端上下文 */
    client_context_t ctx = {0, &loop};
    
    /* 运行事件循环一小段时间以等待连接建立 */
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_DEFAULT);
    }
    
    /* 发送加法请求 */
    int32_t params[2] = {10, 20};
    printf("[CLIENT] Calling Add(10, 20)...\n");
    fflush(stdout);
    uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), add_callback, &ctx);
    
    /* 运行事件循环，收到响应后通过 uv_stop 退出 */
    int iterations = 0;
    while (!ctx.received && iterations < 100) {
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
