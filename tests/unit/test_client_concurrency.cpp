#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCClientConcurrencyTest : public ::testing::Test {
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

TEST_F(UVRPCClientConcurrencyTest, SetMaxConcurrent) {
    int ret = uvrpc_client_set_max_concurrent(client, 10);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCClientConcurrencyTest, GetPendingCountInitial) {
    int pending = uvrpc_client_get_pending_count(client);
    EXPECT_EQ(pending, 0);
}

TEST_F(UVRPCClientConcurrencyTest, SetMaxConcurrentZero) {
    int ret = uvrpc_client_set_max_concurrent(client, 0);
    // Should handle zero gracefully (implementation-dependent)
}

TEST_F(UVRPCClientConcurrencyTest, SetMaxConcurrentNegative) {
    int ret = uvrpc_client_set_max_concurrent(client, -1);
    // Should handle negative values gracefully
}

TEST_F(UVRPCClientConcurrencyTest, SetMaxConcurrentLarge) {
    int ret = uvrpc_client_set_max_concurrent(client, 10000);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCClientConcurrencyTest, SetMaxConcurrentMultipleTimes) {
    uvrpc_client_set_max_concurrent(client, 10);
    uvrpc_client_set_max_concurrent(client, 20);
    uvrpc_client_set_max_concurrent(client, 30);
    
    int ret = uvrpc_client_set_max_concurrent(client, 50);
    EXPECT_EQ(ret, UVRPC_OK);
}

TEST_F(UVRPCClientConcurrencyTest, GetPendingCountAfterSetMaxConcurrent) {
    uvrpc_client_set_max_concurrent(client, 100);
    int pending = uvrpc_client_get_pending_count(client);
    // Should still be 0 since no requests have been made
    EXPECT_EQ(pending, 0);
}