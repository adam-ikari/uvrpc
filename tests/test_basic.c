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

/* 测试 REQ_REP 模式 */
void test_req_rep_mode(test_stats_t* stats) {
    printf("\n--- Test: REQ_REP Mode ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15555", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 注册服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_test_handler, NULL);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Service registration failed");
    
    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:15555", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, client != NULL, "Client creation failed");
    
    /* 发送请求 */
    size_t request_size;
    uint8_t* request_data = create_echo_request("Hello, World!", &request_size);
    ASSERT_TRUE(stats, request_data != NULL, "Request creation failed");
    
    test_callback_ctx_t ctx = {
        .stats = stats,
        .test_name = "REQ_REP",
        .received_response = 0,
        .expected_status = UVRPC_OK,
        .expected_message = "Echo: Hello, World!"
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

/* 测试服务未找到错误 */
void test_service_not_found(test_stats_t* stats) {
    printf("\n--- Test: Service Not Found ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15556", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 不注册任何服务 */
    int rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:15556", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, client != NULL, "Client creation failed");
    
    /* 发送请求到不存在的服务 */
    size_t request_size;
    uint8_t* request_data = create_echo_request("Test", &request_size);
    ASSERT_TRUE(stats, request_data != NULL, "Request creation failed");
    
    test_callback_ctx_t ctx = {
        .stats = stats,
        .test_name = "ServiceNotFound",
        .received_response = 0,
        .expected_status = UVRPC_ERROR_SERVICE_NOT_FOUND,
        .expected_message = NULL
    };
    
    rc = uvrpc_client_call(client, "nonexistent.Service", request_data, request_size,
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

/* 测试多个并发请求 */
void test_concurrent_requests(test_stats_t* stats) {
    printf("\n--- Test: Concurrent Requests ---\n");
    stats->total++;
    
    uv_loop_t* loop = uv_default_loop();
    
    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://127.0.0.1:15557", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, server != NULL, "Server creation failed");
    
    /* 注册服务 */
    int rc = uvrpc_server_register_service(server, "echo.EchoService", echo_test_handler, NULL);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Service registration failed");
    
    /* 启动服务器 */
    rc = uvrpc_server_start(server);
    ASSERT_EQ(stats, rc, UVRPC_OK, "Server start failed");
    
    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:15557", UVRPC_MODE_REQ_REP);
    ASSERT_TRUE(stats, client != NULL, "Client creation failed");
    
    /* 发送多个并发请求 */
    const int num_requests = 5;
    int completed_requests = 0;
    
    for (int i = 0; i < num_requests; i++) {
        char message[64];
        snprintf(message, sizeof(message), "Request %d", i);
        
        size_t request_size;
        uint8_t* request_data = create_echo_request(message, &request_size);
        if (!request_data) {
            continue;
        }
        
        rc = uvrpc_client_call(client, "echo.EchoService", request_data, request_size,
                               NULL, &completed_requests);
        
        free(request_data);
        
        ASSERT_EQ(stats, rc, UVRPC_OK, "RPC call failed");
        stats->passed++;
    }
    
    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_server_free(server);
    
    printf("Test completed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    printf("======================================\n");
    printf("  uvrpc E2E Tests\n");
    printf("======================================\n");
    
    test_stats_t stats = {0, 0, 0};
    
    /* 运行基础测试 */
    test_req_rep_mode(&stats);
    test_service_not_found(&stats);
    test_concurrent_requests(&stats);
    
    /* 打印结果 */
    print_test_summary(&stats);
    
    return 0;
}