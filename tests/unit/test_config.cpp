#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCConfigTest : public ::testing::Test {
protected:
    uvrpc_config_t* config;
    
    void SetUp() override {
        config = uvrpc_config_new();
        ASSERT_NE(config, nullptr);
    }
    
    void TearDown() override {
        uvrpc_config_free(config);
    }
};

TEST_F(UVRPCConfigTest, CreateAndFreeConfig) {
    // Config created in SetUp, freed in TearDown
    EXPECT_NE(config, nullptr);
}

TEST_F(UVRPCConfigTest, DoubleFreeConfig) {
    uvrpc_config_free(config);
    config = nullptr;
    
    // Second free should not crash
    uvrpc_config_free(nullptr);
}

TEST_F(UVRPCConfigTest, SetAddress) {
    uvrpc_config_t* result = uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetNullAddress) {
    uvrpc_config_t* result = uvrpc_config_set_address(config, nullptr);
    EXPECT_EQ(result, nullptr);
}

TEST_F(UVRPCConfigTest, SetEmptyAddress) {
    uvrpc_config_t* result = uvrpc_config_set_address(config, "");
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetCommType) {
    uvrpc_config_t* result = uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    EXPECT_EQ(result, config);
    
    result = uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetTransportType) {
    uvrpc_config_t* result = uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    EXPECT_EQ(result, config);
    
    result = uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetPerformanceMode) {
    uvrpc_config_t* result = uvrpc_config_set_performance_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);
    EXPECT_EQ(result, config);
    
    result = uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetPoolSize) {
    uvrpc_config_t* result = uvrpc_config_set_pool_size(config, 10);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetMaxConcurrent) {
    uvrpc_config_t* result = uvrpc_config_set_max_concurrent(config, 100);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetTimeout) {
    uvrpc_config_t* result = uvrpc_config_set_timeout(config, 5000);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, SetMsgidOffset) {
    uvrpc_config_t* result = uvrpc_config_set_msgid_offset(config, 1000);
    EXPECT_EQ(result, config);
}

TEST_F(UVRPCConfigTest, MultipleConfigurations) {
    // Create multiple config instances
    uvrpc_config_t* config1 = uvrpc_config_new();
    uvrpc_config_t* config2 = uvrpc_config_new();
    
    ASSERT_NE(config1, nullptr);
    ASSERT_NE(config2, nullptr);
    ASSERT_NE(config1, config2);
    
    // Configure them differently
    uvrpc_config_set_address(config1, "tcp://127.0.0.1:5555");
    uvrpc_config_set_address(config2, "tcp://127.0.0.1:6666");
    
    // Cleanup
    uvrpc_config_free(config1);
    uvrpc_config_free(config2);
}

TEST_F(UVRPCConfigTest, SetMultipleOptions) {
    // Set multiple options in sequence
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_performance_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);
    uvrpc_config_set_pool_size(config, 10);
    uvrpc_config_set_max_concurrent(config, 100);
    uvrpc_config_set_timeout(config, 5000);
    
    // All setters should return config pointer
    EXPECT_NE(config, nullptr);
}

TEST_F(UVRPCConfigTest, SetLoop) {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* result = uvrpc_config_set_loop(config, &loop);
    EXPECT_EQ(result, config);
    
    uv_loop_close(&loop);
}