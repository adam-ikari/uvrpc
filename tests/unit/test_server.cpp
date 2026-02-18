#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCServerTest : public ::testing::Test {
protected:
    uvrpc_server_t* server;
    uvrpc_config_t* config;
    uv_loop_t loop;
    
    void SetUp() override {
        uv_loop_init(&loop);
        config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    }
    
    void TearDown() override {
        if (server) {
            uvrpc_server_free(server);
            server = nullptr;
        }
        uvrpc_config_free(config);
        // Run loop to cleanup handles
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

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
    
    uvrpc_server_t* bad_server = uvrpc_server_create(bad_config);
    // Should create but may fail to start later
    uvrpc_config_free(bad_config);
    if (bad_server) {
        uvrpc_server_free(bad_server);
    }
}

TEST_F(UVRPCServerTest, StartServer) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_start(server);
    EXPECT_EQ(ret, UVRPC_OK);
    
    // Run loop briefly to start server
    for (int i = 0; i < 5; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
}

TEST_F(UVRPCServerTest, RegisterHandler) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    // Simple handler that returns OK
    auto handler = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    };
    
    int ret = uvrpc_server_register(server, "test", handler, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullServer) {
    auto handler = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
    };
    
    int ret = uvrpc_server_register(nullptr, "test", handler, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullMethod) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    auto handler = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
    };
    
    int ret = uvrpc_server_register(server, nullptr, handler, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, RegisterHandlerWithNullHandler) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    int ret = uvrpc_server_register(server, "test", nullptr, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCServerTest, GetTotalRequests) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    uint64_t requests = uvrpc_server_get_total_requests(server);
    EXPECT_EQ(requests, 0);
}

TEST_F(UVRPCServerTest, GetTotalResponses) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    uint64_t responses = uvrpc_server_get_total_responses(server);
    EXPECT_EQ(responses, 0);
}

TEST_F(UVRPCServerTest, GetStatsFromNullServer) {
    uint64_t requests = uvrpc_server_get_total_requests(nullptr);
    EXPECT_EQ(requests, 0);
    
    uint64_t responses = uvrpc_server_get_total_responses(nullptr);
    EXPECT_EQ(responses, 0);
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

TEST_F(UVRPCServerTest, MultipleHandlers) {
    server = uvrpc_server_create(config);
    ASSERT_NE(server, nullptr);
    
    auto handler1 = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    };
    
    auto handler2 = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    };
    
    int ret = uvrpc_server_register(server, "method1", handler1, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
    
    ret = uvrpc_server_register(server, "method2", handler2, nullptr);
    EXPECT_EQ(ret, UVRPC_OK);
}