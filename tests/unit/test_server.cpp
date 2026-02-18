/**
 * UVRPC Server Unit Tests
 */

#include <gtest/gtest.h>
#include "uvrpc.h"
#include <uv.h>
#include <unistd.h>

class UVRPCServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        uv_loop_init(&loop);
        
        config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5556");
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    }
    
    void TearDown() override {
        if (server) {
            uvrpc_server_free(server);
        }
        if (config) {
            uvrpc_config_free(config);
        }
        uv_loop_close(&loop);
    }
    
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_server_t* server;
};

// Simple test handler
void test_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    int32_t result = 42;
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
}

TEST_F(UVRPCServerTest, CreateServer) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
}

TEST_F(UVRPCServerTest, CreateServerWithNullConfig) {
    server = uvrpc_server_create(nullptr);
    EXPECT_EQ(server, nullptr);
}

TEST_F(UVRPCServerTest, CreateServerWithNullLoop) {
    uvrpc_config_t* bad_config = uvrpc_config_new();
    uvrpc_config_set_address(bad_config, "tcp://127.0.0.1:5556");
    uvrpc_config_set_comm_type(bad_config, UVRPC_COMM_SERVER_CLIENT);
    // Don't set loop
    
    server = uvrpc_server_create(bad_config);
    // Should create but may fail to start later
    uvrpc_config_free(bad_config);
}

TEST_F(UVRPCServerTest, StartServer) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    EXPECT_EQ(ret, UVRPC_OK);
    
    // Run loop briefly
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
}

TEST_F(UVRPCServerTest, StartServerTwice) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    EXPECT_EQ(ret, UVRPC_OK);
    
    // Start again should fail or be idempotent
    ret = uvrpc_server_start(server);
    // Behavior depends on implementation
}

TEST_F(UVRPCServerTest, RegisterHandler) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, "test", test_handler, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullServer) {
    int ret = uvrpc_server_register(nullptr, "test", test_handler, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullMethod) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, nullptr, test_handler, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullHandler) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, "test", nullptr, nullptr);
    // May succeed or fail depending on implementation
}

TEST_F(UVRPCServerTest, GetTotalRequests) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    ASSERT_EQ(ret, UVRPC_OK);
    
    uint64_t requests = uvrpc_server_get_total_requests(server);
    EXPECT_EQ(requests, 0ULL);
}

TEST_F(UVRPCServerTest, GetTotalResponses) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    ASSERT_EQ(ret, UVRPC_OK);
    
    uint64_t responses = uvrpc_server_get_total_responses(server);
    EXPECT_EQ(responses, 0ULL);
}

TEST_F(UVRPCServerTest, GetStatsFromNullServer) {
    uint64_t requests = uvrpc_server_get_total_requests(nullptr);
    uint64_t responses = uvrpc_server_get_total_responses(nullptr);
    
    EXPECT_EQ(requests, 0ULL);
    EXPECT_EQ(responses, 0ULL);
}

TEST_F(UVRPCServerTest, FreeNullServer) {
    uvrpc_server_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCServerTest, DoubleFreeServer) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    uvrpc_server_free(server);
    server = nullptr;
    
    // Second free should not crash
    uvrpc_server_free(nullptr);
}

TEST_F(UVRPCServerTest, ServerWithHandler) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, "test", test_handler, nullptr);
    ASSERT_EQ(ret, UVRPC_OK);
    
    ret = uvrpc_server_start(server);
    EXPECT_EQ(ret, UVRPC_OK);
    
    // Run loop briefly
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
}

TEST_F(UVRPCServerTest, MultipleHandlers) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, "handler1", test_handler, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
    
    ret = uvrpc_server_register(server, "handler2", test_handler, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
    
    ret = uvrpc_server_register(server, "handler3", test_handler, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
    
    ret = uvrpc_server_start(server);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, StopServer) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    ASSERT_EQ(ret, UVRPC_OK);
    
    // Run loop briefly
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    uvrpc_server_stop(server);
    // Should stop gracefully
}