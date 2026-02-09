#include "test_common.h"
#include <unistd.h>

/* Echo 服务处理器 */
static int echo_test_handler(void* ctx,
                             const uint8_t* request_data,
                             size_t request_size,
                             uint8_t** response_data,
                             size_t* response_size) {
    (void)ctx;
    
    flatbuffers_t* fb = flatbuffers_init(request_data, request_size);
    if (!fb) {
        return UVRPC_ERROR;
    }
    
    echo_EchoRequest_table_t request;
    if (!echo_EchoRequest_as_root(fb)) {
        flatbuffers_clear(fb);
        return UVRPC_ERROR;
    }
    echo_EchoRequest_init(&request, fb);
    
    const char* message = echo_EchoRequest_message(&request);
    
    flatbuffers_builder_t* builder = flatbuffers_builder_init(1024);
    if (!builder) {
        flatbuffers_clear(fb);
        return UVRPC_ERROR;
    }
    
    char reply[256];
    snprintf(reply, sizeof(reply), "Echo: %s", message);
    flatbuffers_string_ref_t reply_ref = flatbuffers_string_create(builder, reply);
    int64_t processed_at = 0;
    
    echo_EchoResponse_start(builder);
    echo_EchoResponse_reply_add(builder, reply_ref);
    echo_EchoResponse_processed_at_add(builder, processed_at);
    flatbuffers_uint8_vec_ref_t response_ref = echo_EchoResponse_end(builder);
    echo_EchoResponse_create_as_root(builder, response_ref);
    
    const uint8_t* data = flatbuffers_builder_get_data(builder, response_size);
    *response_data = (uint8_t*)malloc(*response_size);
    if (*response_data) {
        memcpy(*response_data, data, *response_size);
    }
    
    flatbuffers_builder_clear(builder);
    flatbuffers_clear(fb);
    
    return UVRPC_OK;
}

/* 测试 ROUTER_DEALER 模式 */
void test_router_dealer_mode(test_stats_t* stats) {
    printf("\n--- Test: ROUTER_DEALER Mode ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建 ROUTER 服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15558", UVRPC_MODE_ROUTER_DEALER);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 注册服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_test_handler, NULL);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Service registration failed");
    
    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建 DEALER 客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:15558", UVRPC_MODE_ROUTER_DEALER);
    ASSERT_TRUE(stats, client != NULL, "Client creation failed");
    
    /* 发送请求 */
    size_t request_size;
    uint8_t* request_data = create_echo_request("Hello from DEALER!", &request_size);
    ASSERT_TRUE(stats, request_data != NULL, "Request creation failed");
    
    test_callback_ctx_t ctx = {
        .stats = stats,
        .test_name = "ROUTER_DEALER",
        .received_response = 0,
        .expected_status = UVRPC_OK,
        .expected_message = "Echo: Hello from DEALER!"
    };
    
    rc = uvrpc_client_call(client, "echo.EchoService", request_data, request_size,
                           NULL, &ctx);
    ASSERT_EQ(stats, rc, UVRPC_OK, "RPC call failed");
    
    free(request_data);
    
    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    
    printf("Test completed\n");
}

/* 测试多客户端并发 */
void test_multiple_clients(test_stats_t* stats) {
    printf("\n--- Test: Multiple Clients ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建 ROUTER 服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15559", UVRPC_MODE_ROUTER_DEALER);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 注册服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_test_handler, NULL);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Service registration failed");
    
    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建多个客户端 */
    const int num_clients = 3;
    uvrpc_client_t* clients[num_clients];
    
    for (int i = 0; i < num_clients; i++) {
        clients[i] = uvrpc_client_new(loop, "tcp://127.0.0.1:15559", UVRPC_MODE_ROUTER_DEALER);
        ASSERT_TRUE(stats, clients[i] != NULL, "Client creation failed");
        stats->passed++;
    }
    
    /* 每个客户端发送多个请求 */
    const int requests_per_client = 2;
    int total_requests = num_clients * requests_per_client;
    int completed_requests = 0;
    
    for (int i = 0; i < num_clients; i++) {
        for (int j = 0; j < requests_per_client; j++) {
            char message[64];
            snprintf(message, sizeof(message), "Client %d, Request %d", i, j);
            
            size_t request_size;
            uint8_t* request_data = create_echo_request(message, &request_size);
            if (!request_data) {
                continue;
            }
            
            rc = uvrpc_client_call(clients[i], "echo.EchoService", request_data, request_size,
                                   NULL, &completed_requests);
            
            free(request_data);
            
            ASSERT_EQ(stats, rc, UVRPC_OK, "RPC call failed");
            stats->passed++;
        }
    }
    
    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);
    
    /* 清理客户端 */
    for (int i = 0; i < num_clients; i++) {
        uvrpc_client_free(clients[i]);
    }
    
    /* 清理服务器 */
    uvrpc_server_free(server);
    
    printf("Test completed\n");
}

/* 测试请求 ID 匹配 */
void test_request_id_matching(test_stats_t* stats) {
    printf("\n--- Test: Request ID Matching ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建 ROUTER 服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15560", UVRPC_MODE_ROUTER_DEALER);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 注册服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_test_handler, NULL);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Service registration failed");
    
    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:15560", UVRPC_MODE_ROUTER_DEALER);
    ASSERT_TRUE(stats, client != NULL, "Client creation failed");
    
    /* 发送多个不同消息的请求 */
    const char* messages[] = {"First", "Second", "Third", "Fourth", "Fifth"};
    const int num_requests = 5;
    int received_responses = 0;
    
    for (int i = 0; i < num_requests; i++) {
        size_t request_size;
        uint8_t* request_data = create_echo_request(messages[i], &request_size);
        if (!request_data) {
            continue;
        }
        
        rc = uvrpc_client_call(client, "echo.EchoService", request_data, request_size,
                               NULL, &received_responses);
        
        free(request_data);
        
        ASSERT_EQ(stats, rc, UVRPC_OK, "RPC call failed");
        stats->passed++;
    }
    
    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);
    
    /* 验证所有请求都收到了响应 */
    ASSERT_EQ(stats, received_responses, num_requests, "Not all requests received responses");
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    
    printf("Test completed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    printf("======================================\n");
    printf("  uvrpc ROUTER_DEALER E2E Tests\n");
    printf("======================================\n");
    
    test_stats_t stats = {0, 0, 0};
    
    /* 运行 ROUTER_DEALER 测试 */
    test_router_dealer_mode(&stats);
    test_multiple_clients(&stats);
    test_request_id_matching(&stats);
    
    /* 打印结果 */
    print_test_summary(&stats);
    
    return 0;
}