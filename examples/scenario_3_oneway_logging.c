/**
 * Scenario 3: 单向 RPC (Oneway RPC)
 * 
 * 适用场景：
 * - 日志记录（不需要响应）
 * - 事件通知（fire-and-forget）
 * - 心跳检测（只需发送，无需确认）
 * - 指标上报（异步发送）
 * 
 * 特点：
 * - 只发送请求，不等待响应
 * - 服务器端不发送响应
 * - 高吞吐量，低延迟
 * - 使用 oneway 模式
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 服务器端：日志记录 */
static void log_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size >= 8) {
        int32_t level = *(int32_t*)req->params;
        const char* message = (const char*)(req->params + 8);
        
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        
        const char* level_str = "INFO";
        if (level == 0) level_str = "DEBUG";
        else if (level == 1) level_str = "INFO";
        else if (level == 2) level_str = "WARNING";
        else if (level == 3) level_str = "ERROR";
        
        printf("[%s] [%s] %s\n", timestamp, level_str, message ? message : "(empty)");
    }
    
    /* 注意：oneway 模式不发送响应 */
}

/* 客户端：不需要回调 */

/* 客户端上下文 */
typedef struct {
    int requests_sent;
    uv_loop_t* loop;
} client_context_t;

/* 连接回调 */
static void on_connect(int status, void* ctx) {
    client_context_t* context = (client_context_t*)ctx;
    if (status == 0) {
        printf("[CLIENT] Connected successfully\n");
fflush(stdout);
    } else {
        printf("[CLIENT] Connection failed: %d\n", status);
fflush(stdout);
    }
}

int main(int argc, char** argv) {
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5558";
    
    printf("=== Scenario 3: Oneway RPC ===\n");
    printf("Pattern: Fire-and-forget (no response)\n");
    printf("Use case: Logging, notifications, heartbeat, metrics\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建服务器 */
    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, address);
    
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    uvrpc_server_register(server, "Log", log_handler, NULL);
    uvrpc_server_start(server);
    
    /* 创建客户端 */
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, address);
    
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    
    /* 客户端上下文 */
    client_context_t ctx = {0, &loop};
    
    uvrpc_client_connect_with_callback(client, on_connect, &ctx);
    
    /* 发送多个日志请求（不需要回调） */
    printf("[CLIENT] Sending 5 log messages (no waiting for response)...\n");
    for (int i = 0; i < 5; i++) {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "Application event %d - User action completed", i + 1);
        
        int32_t level = 1;  // INFO level
        
        // 构建 oneway 请求：[level (4 bytes)] + [message]
        char oneway_req[256];
        memcpy(oneway_req, &level, sizeof(level));
        strcpy(oneway_req + 4, log_msg);
        
        uvrpc_client_call(client, "Log", (uint8_t*)oneway_req, 4 + strlen(log_msg) + 1, 
                         NULL, NULL);  // NULL callback = oneway
        
        printf("[CLIENT] Sent log message %d\n", i + 1);
        ctx.requests_sent++;
    }
    
    /* 运行事件循环，短暂运行后退出 */
    int iterations = 0;
    while (iterations < 10) {
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