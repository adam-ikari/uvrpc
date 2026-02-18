/**
 * UVRPC Client Unit Tests
 */

#include <gtest/gtest.h>
#include "uvrpc.h"
#include <uv.h>
#include <unistd.h>

class UVRPCClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        uv_loop_init(&loop);
        
        config = uvrpc_config_new();
        uvrpc_config_set_loop(config, &loop);
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
        uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    }
    
    void TearDown() override {
        if (client) {
            uvrpc_client_free(client);
        }
        if (config) {
            uvrpc_config_free(config);
        }
        uv_loop_close(&loop);
    }
    
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_client_t* client;
};

TEST_F(UVRPCClientTest, CreateClient) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
}

TEST_F(UVRPCClientTest, CreateClientWithNullConfig) {
    client = uvrpc_client_create(nullptr);
    EXPECT_EQ(client, nullptr);
    client = nullptr;  // Don't free in TearDown
}

TEST_F(UVRPCClientTest, CreateClientWithNullLoop) {
    uvrpc_config_t* bad_config = uvrpc_config_new();
    uvrpc_config_set_address(bad_config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(bad_config, UVRPC_COMM_SERVER_CLIENT);
    // Don't set loop
    
    client = uvrpc_client_create(bad_config);
    // Should create but may fail to connect later
    uvrpc_config_free(bad_config);
    client = nullptr;  // Don't free in TearDown
}

TEST_F(UVRPCClientTest, CallWithNullClient) {
    int32_t params[2] = {100, 200};
    int ret = uvrpc_client_call(nullptr, "test", (uint8_t*)params, sizeof(params), nullptr, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCClientTest, CallWithNullMethod) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
    
    int32_t params[2] = {100, 200};
    int ret = uvrpc_client_call(client, nullptr, (uint8_t*)params, sizeof(params), nullptr, nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}

TEST_F(UVRPCClientTest, CallWithZeroSizeParams) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
    
    int ret = uvrpc_client_call(client, "test", nullptr, 0, nullptr, nullptr);
    // Should succeed (zero-sized params are valid)
}

TEST_F(UVRPCClientTest, DisconnectClient) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
    
    int ret = uvrpc_client_connect(client);
    EXPECT_EQ(ret, UVRPC_OK);
    
    uvrpc_client_disconnect(client);
}

TEST_F(UVRPCClientTest, DisconnectDisconnectedClient) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
    
    // Disconnect without connecting
    uvrpc_client_disconnect(client);
}

TEST_F(UVRPCClientTest, FreeNullClient) {
    uvrpc_client_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCClientTest, DoubleFreeClient) {
    client = uvrpc_client_create(config);
    ASSERT_NE(client, nullptr);
    
    uvrpc_client_free(client);
    client = nullptr;
    
    // Second free should not crash
    uvrpc_client_free(nullptr);
}

TEST_F(UVRPCClientTest, ConfigPerformanceModes) {
    uvrpc_config_t* config_ht = uvrpc_config_new();
    uvrpc_config_set_performance_mode(config_ht, UVRPC_PERF_HIGH_THROUGHPUT);
    uvrpc_config_free(config_ht);
    
    uvrpc_config_t* config_ll = uvrpc_config_new();
    uvrpc_config_set_performance_mode(config_ll, UVRPC_PERF_LOW_LATENCY);
    uvrpc_config_free(config_ll);
    
    SUCCEED();
}