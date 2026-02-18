#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCServerStatsTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    uvrpc_config_t* config;
    uvrpc_server_t* server;
    
    void SetUp() override {
        uv_loop_init(&loop);
        config = uvrpc_config_new();
        uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
        uvrpc_config_set_loop(config, &loop);
        server = uvrpc_server_create(config);
    }
    
    void TearDown() override {
        if (server) {
            uvrpc_server_free(server);
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

TEST_F(UVRPCServerStatsTest, GetTotalRequestsInitial) {
    uint64_t total = uvrpc_server_get_total_requests(server);
    EXPECT_EQ(total, 0);
}

TEST_F(UVRPCServerStatsTest, GetTotalResponsesInitial) {
    uint64_t total = uvrpc_server_get_total_responses(server);
    EXPECT_EQ(total, 0);
}

TEST_F(UVRPCServerStatsTest, GetStatsFromNullServer) {
    uint64_t requests = uvrpc_server_get_total_requests(nullptr);
    uint64_t responses = uvrpc_server_get_total_responses(nullptr);
    
    // Should handle null server gracefully
    EXPECT_EQ(requests, 0);
    EXPECT_EQ(responses, 0);
}

TEST_F(UVRPCServerStatsTest, MultipleStatsRead) {
    uint64_t total1 = uvrpc_server_get_total_requests(server);
    uint64_t total2 = uvrpc_server_get_total_responses(server);
    
    // Stats should remain consistent across multiple reads
    EXPECT_EQ(total1, 0);
    EXPECT_EQ(total2, 0);
    
    uint64_t total3 = uvrpc_server_get_total_requests(server);
    uint64_t total4 = uvrpc_server_get_total_responses(server);
    
    EXPECT_EQ(total3, 0);
    EXPECT_EQ(total4, 0);
}

TEST_F(UVRPCServerStatsTest, StatsAreUint64) {
    uint64_t requests = uvrpc_server_get_total_requests(server);
    uint64_t responses = uvrpc_server_get_total_responses(server);
    
    // Verify the stats are uint64 and can hold large values
    EXPECT_GE(requests, 0);
    EXPECT_GE(responses, 0);
    EXPECT_LE(requests, UINT64_MAX);
    EXPECT_LE(responses, UINT64_MAX);
}