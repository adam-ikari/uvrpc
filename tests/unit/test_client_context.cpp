#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCClientContextTest : public ::testing::Test {
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

TEST_F(UVRPCClientContextTest, SetContext) {
    void* data = malloc(100);
    uvrpc_context_t* ctx = uvrpc_context_new(data);
    
    uvrpc_client_set_context(client, ctx);
    uvrpc_context_t* retrieved = uvrpc_client_get_context(client);
    
    EXPECT_EQ(retrieved, ctx);
    EXPECT_EQ(uvrpc_context_get_data(retrieved), data);
    
    free(data);
}

TEST_F(UVRPCClientContextTest, SetContextWithCleanup) {
    bool cleanup_called = false;
    void* data = malloc(100);
    
    auto cleanup = [](void* data, void* user_data) {
        if (data) {
            free(data);
        }
        bool* cleanup_called = (bool*)user_data;
        if (cleanup_called) {
            *cleanup_called = true;
        }
    };
    
    uvrpc_context_t* ctx = uvrpc_context_new_with_cleanup(data, cleanup, &cleanup_called);
    uvrpc_client_set_context(client, ctx);
    
    // Client will free the context when it's freed
}

TEST_F(UVRPCClientContextTest, GetContextWhenNotSet) {
    uvrpc_context_t* ctx = uvrpc_client_get_context(client);
    EXPECT_EQ(ctx, nullptr);
}

TEST_F(UVRPCClientContextTest, SetNullContext) {
    uvrpc_client_set_context(client, nullptr);
    uvrpc_context_t* ctx = uvrpc_client_get_context(client);
    EXPECT_EQ(ctx, nullptr);
}

TEST_F(UVRPCClientContextTest, ReplaceContext) {
    void* data1 = malloc(100);
    uvrpc_context_t* ctx1 = uvrpc_context_new(data1);
    
    uvrpc_client_set_context(client, ctx1);
    uvrpc_context_t* retrieved1 = uvrpc_client_get_context(client);
    EXPECT_EQ(retrieved1, ctx1);
    
    // Replace with new context
    void* data2 = malloc(100);
    uvrpc_context_t* ctx2 = uvrpc_context_new(data2);
    uvrpc_client_set_context(client, ctx2);
    
    uvrpc_context_t* retrieved2 = uvrpc_client_get_context(client);
    EXPECT_EQ(retrieved2, ctx2);
    
    free(data1);
    free(data2);
    uvrpc_context_free(ctx1);
}