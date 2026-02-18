/**
 * UVRPC 完整示例
 * 
 * 本示例展示了 UVRPC 的所有主要功能：
 * 1. 客户端-服务器（CS）模式
 * 2. 发布-订阅（广播）模式
 * 3. 所有传输协议（TCP、UDP、IPC、INPROC）
 * 4. 多客户端并发
 * 5. 错误处理
 * 
 * 编译：
 * gcc -o complete_example complete_example.c -I../include -L../dist/lib -luvrpc -luv
 */

#include "../include/uvrpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* 全局标志，用于优雅关闭 */
static volatile sig_atomic_t g_running = 1;

/* 信号处理函数 */
void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

/* ============================================================
 * 客户端-服务器（CS）模式示例
 * ============================================================ */

/**
 * 加法处理器 - 演示参数解析和响应发送
 */
void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    /* 解析参数：期望两个 int32_t */
    if (req->params_size < sizeof(int32_t) * 2) {
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
        uvrpc_request_free(req);
        return;
    }
    
    int32_t a = *(int32_t*)req->params;
    int32_t b = *(int32_t*)(req->params + sizeof(int32_t));
    int32_t result = a + b;
    
    /* 检查整数溢出 */
    if (a > 0 && b > 0 && result < 0) {
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
        uvrpc_request_free(req);
        return;
    }
    if (a < 0 && b < 0 && result > 0) {
        uvrpc_request_send_response(req, UVRPC_ERROR_INVALID_PARAM, NULL, 0);
        uvrpc_request_free(req);
        return;
    }
    
    /* 发送响应 */
    uvrpc_request_send_response(req, UVRPC_OK, 
                                 (uint8_t*)&result, sizeof(result));
    uvrpc_request_free(req);
    
    printf("[SERVER] Add: %d + %d = %d\n", a, b, result);
}

/**
 * Echo 处理器 - 演示简单的请求-响应
 */
void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    
    /* 将请求原样返回 */
    uvrpc_request_send_response(req, UVRPC_OK, 
                                 req->params, req->params_size);
    uvrpc_request_free(req);
    
    printf("[SERVER] Echo: %.*s\n", (int)req->params_size, req->params);
}

/**
 * 启动 CS 模式服务器
 */
int start_cs_server(const char* address) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }
    
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(config);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* 注册处理器 */
    uvrpc_server_register(server, "Add", add_handler, NULL);
    uvrpc_server_register(server, "Echo", echo_handler, NULL);
    
    /* 启动服务器 */
    int ret = uvrpc_server_start(server);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start server: %d\n", ret);
        uvrpc_server_free(server);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[SERVER] Running on %s\n", address);
    printf("[SERVER] Press Ctrl+C to stop\n");
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[SERVER] Stopped\n");
    return 0;
}

/**
 * 客户端响应回调
 */
void cs_response_callback(uvrpc_response_t* resp, void* ctx) {
    int* call_count = (int*)ctx;
    (*call_count)++;
    
    if (resp->status == UVRPC_OK) {
        if (resp->result_size >= sizeof(int32_t)) {
            int32_t result = *(int32_t*)resp->result;
            printf("[CLIENT] Response #%d: %d\n", *call_count, result);
        }
    } else {
        printf("[CLIENT] Error: %d\n", resp->status);
    }
    
    uvrpc_response_free(resp);
}

/**
 * 启动 CS 模式客户端
 */
int start_cs_client(const char* address) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }
    
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_create(config);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* 连接服务器 */
    int ret = uvrpc_client_connect(client);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to connect: %d\n", ret);
        uvrpc_client_free(client);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[CLIENT] Connected to %s\n", address);
    
    /* 发送多个请求 */
    int call_count = 0;
    for (int i = 0; i < 10 && g_running; i++) {
        /* 调用 Add 方法 */
        int32_t params[2] = {i, i * 2};
        uvrpc_client_call(client, "Add", 
                          (uint8_t*)params, sizeof(params),
                          cs_response_callback, &call_count);
        
        /* 运行事件循环处理响应 */
        for (int j = 0; j < 10 && g_running; j++) {
            uv_run(&loop, UV_RUN_ONCE);
        }
    }
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[CLIENT] Stopped, sent %d requests\n", call_count);
    return 0;
}

/* ============================================================
 * 发布-订阅（广播）模式示例
 * ============================================================ */

/**
 * 发布者发布回调
 */
void publish_callback(int status, void* ctx) {
    int* publish_count = (int*)ctx;
    
    if (status == UVRPC_OK) {
        (*publish_count)++;
    } else {
        printf("[PUBLISHER] Publish failed: %d\n", status);
    }
}

/**
 * 启动广播发布者
 */
int start_broadcaster(const char* address) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }
    
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* 创建发布者 */
    uvrpc_publisher_t* publisher = uvrpc_publisher_create(config);
    if (!publisher) {
        fprintf(stderr, "Failed to create publisher\n");
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* 启动发布者 */
    int ret = uvrpc_publisher_start(publisher);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to start publisher: %d\n", ret);
        uvrpc_publisher_free(publisher);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[PUBLISHER] Running on %s\n", address);
    printf("[PUBLISHER] Publishing to topic: 'news'\n");
    
    /* 发布消息 */
    int publish_count = 0;
    int message_num = 0;
    
    while (g_running) {
        char message[256];
        snprintf(message, sizeof(message), "News #%d: Hello from UVRPC!", message_num);
        
        uvrpc_publisher_publish(publisher, "news", 
                               (const uint8_t*)message, strlen(message),
                               publish_callback, &publish_count);
        
        printf("[PUBLISHER] Published: %s\n", message);
        
        /* 运行事件循环 */
        for (int i = 0; i < 10 && g_running; i++) {
            uv_run(&loop, UV_RUN_ONCE);
        }
        
        message_num++;
        if (message_num >= 10) break;  /* 只发布 10 条消息 */
    }
    
    /* 清理 */
    uvrpc_publisher_stop(publisher);
    uvrpc_publisher_free(publisher);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[PUBLISHER] Stopped, published %d messages\n", publish_count);
    return 0;
}

/**
 * 订阅者订阅回调
 */
void subscribe_callback(const char* topic, const uint8_t* data, 
                        size_t size, void* ctx) {
    int* subscribe_count = (int*)ctx;
    (*subscribe_count)++;
    
    printf("[SUBSCRIBER] [%s] #%d: %.*s\n", 
           topic, *subscribe_count, (int)size, data);
}

/**
 * 启动广播订阅者
 */
int start_subscriber(const char* address) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }
    
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, address);
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    /* 创建订阅者 */
    uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
    if (!subscriber) {
        fprintf(stderr, "Failed to create subscriber\n");
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* 连接发布者 */
    int ret = uvrpc_subscriber_connect(subscriber);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to connect: %d\n", ret);
        uvrpc_subscriber_free(subscriber);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    /* 订阅主题 */
    ret = uvrpc_subscriber_subscribe(subscriber, "news", 
                                     subscribe_callback, NULL);
    if (ret != UVRPC_OK) {
        fprintf(stderr, "Failed to subscribe: %d\n", ret);
        uvrpc_subscriber_disconnect(subscriber);
        uvrpc_subscriber_free(subscriber);
        uvrpc_config_free(config);
        uv_loop_close(&loop);
        return 1;
    }
    
    printf("[SUBSCRIBER] Connected to %s\n", address);
    printf("[SUBSCRIBER] Subscribed to topic: 'news'\n");
    printf("[SUBSCRIBER] Press Ctrl+C to stop\n");
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_subscriber_unsubscribe(subscriber, "news");
    uvrpc_subscriber_disconnect(subscriber);
    uvrpc_subscriber_free(subscriber);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    printf("[SUBSCRIBER] Stopped\n");
    return 0;
}

/* ============================================================
 * 主函数
 * ============================================================ */

void print_usage(const char* prog_name) {
    printf("UVRPC Complete Example\n\n");
    printf("Usage: %s [mode] [transport] [address]\n\n", prog_name);
    printf("Modes:\n");
    printf("  server       - Start CS mode server\n");
    printf("  client       - Start CS mode client\n");
    printf("  publisher    - Start broadcast publisher\n");
    printf("  subscriber   - Start broadcast subscriber\n\n");
    printf("Transports (address prefix):\n");
    printf("  tcp://       - TCP transport (default)\n");
    printf("  udp://       - UDP transport\n");
    printf("  ipc://       - IPC transport\n");
    printf("  inproc://    - INPROC transport\n\n");
    printf("Examples:\n");
    printf("  %s server tcp://127.0.0.1:5555\n", prog_name);
    printf("  %s client tcp://127.0.0.1:5555\n", prog_name);
    printf("  %s publisher udp://127.0.0.1:6000\n", prog_name);
    printf("  %s subscriber udp://127.0.0.1:6000\n", prog_name);
    printf("  %s server ipc:///tmp/uvrpc.sock\n", prog_name);
    printf("  %s server inproc://test\n\n", prog_name);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    const char* mode = argv[1];
    const char* address = "tcp://127.0.0.1:5555";  /* 默认地址 */
    
    /* 解析地址 */
    if (argc >= 3) {
        address = argv[2];
    }
    
    printf("=== UVRPC Complete Example ===\n");
    printf("Mode: %s\n", mode);
    printf("Address: %s\n\n", address);
    
    int ret = 0;
    
    /* 根据模式执行 */
    if (strcmp(mode, "server") == 0) {
        ret = start_cs_server(address);
    } else if (strcmp(mode, "client") == 0) {
        ret = start_cs_client(address);
    } else if (strcmp(mode, "publisher") == 0) {
        ret = start_broadcaster(address);
    } else if (strcmp(mode, "subscriber") == 0) {
        ret = start_subscriber(address);
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        ret = 1;
    }
    
    printf("\n=== Example Complete ===\n");
    return ret;
}
