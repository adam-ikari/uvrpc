/**
 * Scenario 4: 广播模式 (Broadcast Mode)
 * 
 * 适用场景：
 * - 消息推送（如新闻、通知）
 * - 事件广播（如系统告警）
 * - 实时状态更新（如股票价格）
 * - 多订阅者系统
 * 
 * 特点：
 * - 一个发布者，多个订阅者
 * - 发布者不关心有多少订阅者
 * - 订阅者可以随时加入/离开
 * - 使用 UDP 传输层
 */

#include <uvrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 服务器端：消息发布者 */
static void publish_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    if (req->params_size > 0) {
        char* message = (char*)req->params;
        printf("[PUBLISHER] Broadcasting: %s\n", message);
        
        // 广播模式：只发送一次，所有订阅者都会收到
        uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)message, strlen(message) + 1);
    }
}

/* 订阅者上下文 */
typedef struct {
    const char* subscriber_id;
    int messages_received;
    uv_loop_t* loop;
} subscriber_context_t;

/* 订阅者回调 */
static void subscriber_callback(uvrpc_response_t* resp, void* ctx) {
    subscriber_context_t* context = (subscriber_context_t*)ctx;
    
    if (resp->status == UVRPC_OK && resp->result && resp->result_size > 0) {
        printf("[SUBSCRIBER %s] Received broadcast: %s\n", 
               context->subscriber_id ? context->subscriber_id : "unknown",
               (char*)resp->result);
        fflush(stdout);
        context->messages_received++;
        
        /* 每个订阅者收到 3 条消息后停止 */
        if (context->messages_received >= 3) {
            uv_stop(context->loop);
        }
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
    const char* address = argc > 1 ? argv[1] : "tcp://127.0.0.1:5559";
    
    printf("=== Scenario 4: Broadcast Mode ===\n");
    printf("Pattern: One publisher -> Multiple subscribers\n");
    printf("Use case: News, notifications, real-time updates\n\n");
    
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建发布者服务器 */
    uvrpc_config_t* publisher_config = uvrpc_config_new();
    uvrpc_config_set_loop(publisher_config, &loop);
    uvrpc_config_set_address(publisher_config, address);
    
    uvrpc_server_t* publisher = uvrpc_server_create(publisher_config);
    uvrpc_server_register(publisher, "Broadcast", publish_handler, NULL);
    uvrpc_server_start(publisher);
    printf("[SETUP] Publisher started on %s\n\n", address);
    
    /* 创建订阅者 1 */
    uvrpc_config_t* sub1_config = uvrpc_config_new();
    uvrpc_config_set_loop(sub1_config, &loop);
    uvrpc_config_set_address(sub1_config, address);
    
    /* 订阅者上下文 */
    subscriber_context_t sub1_ctx = {"Subscriber1", 0, &loop};
    subscriber_context_t sub2_ctx = {"Subscriber2", 0, &loop};
    
    uvrpc_client_t* sub1 = uvrpc_client_create(sub1_config);
    uvrpc_client_connect_with_callback(sub1, on_connect, "Subscriber1");
    printf("[SETUP] Subscriber 1 connected\n");
    
    /* 创建订阅者 2 */
    uvrpc_config_t* sub2_config = uvrpc_config_new();
    uvrpc_config_set_loop(sub2_config, &loop);
    uvrpc_config_set_address(sub2_config, address);
    
    uvrpc_client_t* sub2 = uvrpc_client_create(sub2_config);
    uvrpc_client_connect_with_callback(sub2, on_connect, "Subscriber2");
    printf("[SETUP] Subscriber 2 connected\n\n");
    
    /* 发布消息 */
    printf("[PUBLISHER] Broadcasting 3 messages...\n");
    for (int i = 0; i < 3; i++) {
        char message[128];
        snprintf(message, sizeof(message), "Broadcast message #%d - %s", 
                 i + 1, "System update available");
        
        uvrpc_client_call(sub1, "Broadcast", (uint8_t*)message, strlen(message) + 1,
                         subscriber_callback, &sub1_ctx);
        uvrpc_client_call(sub2, "Broadcast", (uint8_t*)message, strlen(message) + 1,
                         subscriber_callback, &sub2_ctx);
        
        sleep(1);
    }
    
    /* 运行事件循环，收到所有广播后通过 uv_stop 退出 */
    int iterations = 0;
    while ((sub1_ctx.messages_received < 3 || sub2_ctx.messages_received < 3) && iterations < 100) {
        uv_run(&loop, UV_RUN_DEFAULT);
        uv_stop(&loop);
        iterations++;
    }
    
    /* 清理 */
    uvrpc_client_free(sub2);
    uvrpc_config_free(sub2_config);
    uvrpc_client_free(sub1);
    uvrpc_config_free(sub1_config);
    uvrpc_server_free(publisher);
    uvrpc_config_free(publisher_config);
    uv_loop_close(&loop);
    
    printf("=== Demo Complete ===\n");
    return 0;
}
