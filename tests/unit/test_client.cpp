/**
 * UVRPC Client Unit Tests
 * 
 * Note: These tests do NOT use libuv event loop to keep unit tests fast and isolated.
 * Integration tests with libuv are in tests/integration/.
 */

#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCClientTest : public ::testing::Test {
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

/* Test client creation with NULL config - should return NULL */
TEST_F(UVRPCClientTest, CreateClientWithNullConfig) {
    uvrpc_client_t* client = uvrpc_client_create(nullptr);
    EXPECT_EQ(client, nullptr);
}

/* Test client call with NULL client - should return error */
TEST_F(UVRPCClientTest, CallWithNullClient) {
    int32_t params[2] = {100, 200};
    int ret = uvrpc_client_call(nullptr, "test", (uint8_t*)params, sizeof(params), nullptr, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

/* Test client disconnect with NULL client - should not crash */
TEST_F(UVRPCClientTest, DisconnectNullClient) {
    uvrpc_client_disconnect(nullptr);
    SUCCEED();
}

/* Test client free with NULL client - should not crash */
TEST_F(UVRPCClientTest, FreeNullClient) {
    uvrpc_client_free(nullptr);
    SUCCEED();
}

/* Test config creation and destruction */
TEST_F(UVRPCClientTest, CreateAndDestroyConfig) {
    uvrpc_config_t* test_config = uvrpc_config_new();
    ASSERT_NE(test_config, nullptr);
    
    /* Verify default values */
    EXPECT_EQ(test_config->transport, UVRPC_TRANSPORT_TCP);
    EXPECT_EQ(test_config->performance_mode, UVRPC_PERF_LOW_LATENCY);
    EXPECT_EQ(test_config->pool_size, UVRPC_DEFAULT_POOL_SIZE);
    EXPECT_EQ(test_config->max_concurrent, UVRPC_MAX_CONCURRENT_REQUESTS);
    EXPECT_EQ(test_config->max_pending_callbacks, UVRPC_DEFAULT_PENDING_CALLBACKS);
    EXPECT_EQ(test_config->timeout_ms, 0);
    EXPECT_EQ(test_config->msgid_offset, 0);
    
    uvrpc_config_free(test_config);
}

/* Test config setters return valid pointers */
TEST_F(UVRPCClientTest, ConfigSetters) {
    uvrpc_config_t* test_config = uvrpc_config_new();
    ASSERT_NE(test_config, nullptr);
    
    /* Test all setters return the config for chaining */
    EXPECT_EQ(uvrpc_config_set_loop(test_config, nullptr), test_config);
    EXPECT_EQ(uvrpc_config_set_address(test_config, "tcp://127.0.0.1:5555"), test_config);
    EXPECT_EQ(uvrpc_config_set_transport(test_config, UVRPC_TRANSPORT_TCP), test_config);
    EXPECT_EQ(uvrpc_config_set_comm_type(test_config, UVRPC_COMM_SERVER_CLIENT), test_config);
    EXPECT_EQ(uvrpc_config_set_performance_mode(test_config, UVRPC_PERF_HIGH_THROUGHPUT), test_config);
    EXPECT_EQ(uvrpc_config_set_pool_size(test_config, 10), test_config);
    EXPECT_EQ(uvrpc_config_set_max_concurrent(test_config, 100), test_config);
    EXPECT_EQ(uvrpc_config_set_max_pending_callbacks(test_config, 65536), test_config);
    EXPECT_EQ(uvrpc_config_set_timeout(test_config, 5000), test_config);
    EXPECT_EQ(uvrpc_config_set_msgid_offset(test_config, 1000), test_config);
    
    uvrpc_config_free(test_config);
}

/* Test config with invalid max_pending_callbacks (not power of 2) */
TEST_F(UVRPCClientTest, ConfigInvalidMaxPending) {
    uvrpc_config_t* test_config = uvrpc_config_new();
    ASSERT_NE(test_config, nullptr);
    
    /* Set invalid value (not power of 2) - should use default */
    uvrpc_config_set_max_pending_callbacks(test_config, 100);
    EXPECT_EQ(test_config->max_pending_callbacks, UVRPC_DEFAULT_PENDING_CALLBACKS);
    
    /* Set valid value (power of 2) - should use the value */
    uvrpc_config_set_max_pending_callbacks(test_config, 128);
    EXPECT_EQ(test_config->max_pending_callbacks, 128);
    
    uvrpc_config_free(test_config);
}

/* Test config auto-detects transport from address */
TEST_F(UVRPCClientTest, ConfigAutoDetectTransport) {
    uvrpc_config_t* test_config = uvrpc_config_new();
    ASSERT_NE(test_config, nullptr);
    
    /* TCP address */
    uvrpc_config_set_address(test_config, "tcp://127.0.0.1:5555");
    EXPECT_EQ(test_config->transport, UVRPC_TRANSPORT_TCP);
    
    /* IPC address */
    uvrpc_config_set_address(test_config, "ipc:///tmp/test.sock");
    EXPECT_EQ(test_config->transport, UVRPC_TRANSPORT_IPC);
    
    /* INPROC address */
    uvrpc_config_set_address(test_config, "inproc://test");
    EXPECT_EQ(test_config->transport, UVRPC_TRANSPORT_INPROC);
    
    /* UDP address */
    uvrpc_config_set_address(test_config, "udp://127.0.0.1:5555");
    EXPECT_EQ(test_config->transport, UVRPC_TRANSPORT_UDP);
    
    uvrpc_config_free(test_config);
}

/* Test error codes are properly defined */
TEST_F(UVRPCClientTest, ErrorCodes) {
    EXPECT_EQ(UVRPC_OK, 0);
    EXPECT_LT(UVRPC_ERROR_INVALID_PARAM, 0);
    EXPECT_LT(UVRPC_ERROR_NO_MEMORY, 0);
    EXPECT_LT(UVRPC_ERROR_TRANSPORT, 0);
    EXPECT_LT(UVRPC_ERROR_NOT_CONNECTED, 0);
    EXPECT_LT(UVRPC_ERROR_CALLBACK_LIMIT, 0);
}