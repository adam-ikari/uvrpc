#include <gtest/gtest.h>
#include "uvrpc_async.h"

class UVRPCAsyncTest : public ::testing::Test {
protected:
    uv_loop_t loop;
    
    void SetUp() override {
        uv_loop_init(&loop);
    }
    
    void TearDown() override {
        // Run loop to cleanup
        for (int i = 0; i < 10; i++) {
            uv_run(&loop, UV_RUN_NOWAIT);
        }
        uv_loop_close(&loop);
    }
};

TEST_F(UVRPCAsyncTest, CreateAsyncContext) {
    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);
    uvrpc_async_ctx_free(ctx);
}

TEST_F(UVRPCAsyncTest, CreateAsyncContextWithNullLoop) {
    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(nullptr);
    EXPECT_EQ(ctx, nullptr);
}

TEST_F(UVRPCAsyncTest, FreeNullAsyncContext) {
    uvrpc_async_ctx_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCAsyncTest, FreeNullAsyncResult) {
    uvrpc_async_result_free(nullptr);
    // Should not crash
}

TEST_F(UVRPCAsyncTest, GetPendingCount) {
    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);
    
    int count = uvrpc_async_get_pending_count(ctx);
    EXPECT_EQ(count, 0);
    
    uvrpc_async_ctx_free(ctx);
}

TEST_F(UVRPCAsyncTest, GetPendingCountFromNullContext) {
    int count = uvrpc_async_get_pending_count(nullptr);
    EXPECT_EQ(count, 0);
}

TEST_F(UVRPCAsyncTest, CancelAllFromNullContext) {
    int ret = uvrpc_async_cancel_all(nullptr);
    EXPECT_NE(ret, UVRPC_OK);
}