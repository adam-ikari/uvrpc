#include <gtest/gtest.h>
#include "uvrpc.h"

class UVRPCContextTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    
    void SetUp() override {
        uv_loop_init(&loop);
    }
    
    void TearDown() override {
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

TEST_F(UVRPCContextTest, CreateContext) {
    void* data = malloc(100);
    uvrpc_context_t* ctx = uvrpc_context_new(data);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(uvrpc_context_get_data(ctx), data);
    uvrpc_context_free(ctx);
    free(data);
}

TEST_F(UVRPCContextTest, CreateContextWithNullData) {
    uvrpc_context_t* ctx = uvrpc_context_new(nullptr);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(uvrpc_context_get_data(ctx), nullptr);
    uvrpc_context_free(ctx);
}

TEST_F(UVRPCContextTest, CreateContextWithCleanup) {
    bool cleanup_called = false;
    void* data = malloc(100);
    
    auto cleanup = [](void* data, void* user_data) {
        (void)user_data;
        if (data) {
            free(data);
        }
        bool* cleanup_called = (bool*)user_data;
        if (cleanup_called) {
            *cleanup_called = true;
        }
    };
    
    uvrpc_context_t* ctx = uvrpc_context_new_with_cleanup(data, cleanup, &cleanup_called);
    ASSERT_NE(ctx, nullptr);
    uvrpc_context_free(ctx);
    
    EXPECT_TRUE(cleanup_called);
}

TEST_F(UVRPCContextTest, CreateContextWithCleanupNullData) {
    bool cleanup_called = false;
    
    auto cleanup = [](void* data, void* user_data) {
        (void)data;
        bool* cleanup_called = (bool*)user_data;
        if (cleanup_called) {
            *cleanup_called = true;
        }
    };
    
    uvrpc_context_t* ctx = uvrpc_context_new_with_cleanup(nullptr, cleanup, &cleanup_called);
    ASSERT_NE(ctx, nullptr);
    uvrpc_context_free(ctx);
    
    // NOTE: Cleanup callback is only called when data is not null
    // See src/uvrpc_context.c:34 - if (ctx->cleanup && ctx->data)
    EXPECT_FALSE(cleanup_called);
}

TEST_F(UVRPCContextTest, CreateContextWithNullCleanup) {
    void* data = malloc(100);
    uvrpc_context_t* ctx = uvrpc_context_new_with_cleanup(data, nullptr, nullptr);
    ASSERT_NE(ctx, nullptr);
    uvrpc_context_free(ctx);
    free(data);  // Need to free manually since cleanup is null
}

TEST_F(UVRPCContextTest, FreeNullContext) {
    uvrpc_context_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCContextTest, GetDataFromNullContext) {
    void* data = uvrpc_context_get_data(nullptr);
    EXPECT_EQ(data, nullptr);
}

TEST_F(UVRPCContextTest, MultipleContextCreation) {
    const int count = 10;
    uvrpc_context_t* contexts[count];
    
    for (int i = 0; i < count; i++) {
        void* data = malloc(100);
        contexts[i] = uvrpc_context_new(data);
        ASSERT_NE(contexts[i], nullptr);
    }
    
    for (int i = 0; i < count; i++) {
        free(uvrpc_context_get_data(contexts[i]));
        uvrpc_context_free(contexts[i]);
    }
}