#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCClientRetryTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_client_t* client;
    
    void SetUp() override {
        uv_loop_init(&loop);
        config = uvrpc_config_new();
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
        uvrpc_config_set_loop(config, &loop);
        client = uvrpc_client_create(config);
    }
    
    void TearDown() override {
        if (client) {
            uvrpc_client_free(client);
        }
        if (config) {
            uvrpc_config_free(config);
        }
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

TEST_F(UVRPCClientRetryTest, SetMaxRetries) {
    int ret = uvrpc_client_set_max_retries(client, 5);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCClientRetryTest, GetMaxRetries) {
    uvrpc_client_set_max_retries(client, 3);
    int max_retries = uvrpc_client_get_max_retries(client);
    EXPECT_EQ(max_retries, 3);
}

TEST_F(UVRPCClientRetryTest, SetMaxRetriesZero) {
    int ret = uvrpc_client_set_max_retries(client, 0);
    EXPECT_EQ(ret, UVRPC_OK);
    
    int max_retries = uvrpc_client_get_max_retries(client);
    EXPECT_EQ(max_retries, 0);
}

TEST_F(UVRPCClientRetryTest, SetMaxRetriesNegative) {
    int ret = uvrpc_client_set_max_retries(client, -1);
    // Should handle negative values gracefully
}

TEST_F(UVRPCClientRetryTest, SetMaxRetriesLarge) {
    int ret = uvrpc_client_set_max_retries(client, 1000);
    EXPECT_EQ(ret, UVRPC_OK);
    
    int max_retries = uvrpc_client_get_max_retries(client);
    EXPECT_EQ(max_retries, 1000);
}

TEST_F(UVRPCClientRetryTest, GetDefaultMaxRetries) {
    int max_retries = uvrpc_client_get_max_retries(client);
    // Should have a default value (implementation-dependent)
    EXPECT_GE(max_retries, 0);
}

TEST_F(UVRPCClientRetryTest, UpdateMaxRetries) {
    uvrpc_client_set_max_retries(client, 3);
    int max_retries1 = uvrpc_client_get_max_retries(client);
    EXPECT_EQ(max_retries1, 3);
    
    uvrpc_client_set_max_retries(client, 7);
    int max_retries2 = uvrpc_client_get_max_retries(client);
    EXPECT_EQ(max_retries2, 7);
}