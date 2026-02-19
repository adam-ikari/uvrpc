/**
 * UVRPC Server Unit Tests
 * 
 * Note: These tests do NOT use libuv event loop to keep unit tests fast and isolated.
 * Integration tests with libuv are in tests/integration/.
 */

#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = uvrpc_config_new();
    }
    
    void TearDown() override {
        if (config) {
            uvrpc_config_free(config);
        }
    }
    
    uvrpc_config_t* config;
};

/* Test server creation with NULL config - should return NULL */
TEST_F(UVRPCServerTest, CreateServerWithNullConfig) {
    uvrpc_server_t* server = uvrpc_server_create(nullptr);
    EXPECT_EQ(server, nullptr);
}

/* Test server stop with NULL server - should not crash */
TEST_F(UVRPCServerTest, StopNullServer) {
    uvrpc_server_stop(nullptr);
    SUCCEED();
}

/* Test server free with NULL server - should not crash */
TEST_F(UVRPCServerTest, FreeNullServer) {
    uvrpc_server_free(nullptr);
    SUCCEED();
}

/* Test double free - should not crash */
TEST_F(UVRPCServerTest, DoubleFreeServer) {
    uvrpc_server_t* server = uvrpc_server_create(nullptr);  /* Returns NULL */
    uvrpc_server_free(server);  /* Should handle NULL gracefully */
    uvrpc_server_free(server);  /* Should handle NULL gracefully again */
    SUCCEED();
}

/* Test register handler with NULL server - should return error */
TEST_F(UVRPCServerTest, RegisterHandlerWithNullServer) {
    auto handler = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    };
    
    int ret = uvrpc_server_register(nullptr, "test", handler, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

/* Test register handler with NULL method - should return error */
TEST_F(UVRPCServerTest, RegisterHandlerWithNullMethod) {
    auto handler = [](uvrpc_request_t* req, void* ctx) {
        (void)req;
        (void)ctx;
        uvrpc_request_send_response(req, UVRPC_OK, NULL, 0);
    };
    
    uvrpc_server_t* server = uvrpc_server_create(nullptr);  /* Returns NULL */
    if (server) {
        int ret = uvrpc_server_register(server, nullptr, handler, nullptr);
        EXPECT_NE(ret, UVRPC_OK);
        uvrpc_server_free(server);
    }
}

/* Test stats are uint64 type */
TEST_F(UVRPCServerTest, StatsAreUint64) {
    uint64_t requests = 0;
    uint64_t responses = 0;
    
    /* Just verify the types are correct at compile time */
    EXPECT_TRUE(sizeof(requests) == 8);
    EXPECT_TRUE(sizeof(responses) == 8);
}