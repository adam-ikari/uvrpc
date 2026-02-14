/**
 * UVRPC Message ID Unit Tests
 */

#include <gtest/gtest.h>
#include "../src/uvrpc_msgid.h"

class MsgIDTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx = uvrpc_msgid_ctx_new();
        ASSERT_NE(ctx, nullptr);
    }
    
    void TearDown() override {
        if (ctx) {
            uvrpc_msgid_ctx_free(ctx);
            ctx = nullptr;
        }
    }
    
    uvrpc_msgid_ctx_t* ctx;
};

TEST_F(MsgIDTest, GenerateSequentialIDs) {
    uint64_t id1 = uvrpc_msgid_next(ctx);
    uint64_t id2 = uvrpc_msgid_next(ctx);
    uint64_t id3 = uvrpc_msgid_next(ctx);
    
    // IDs should be unique and sequential
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
    
    // Should be sequential (incrementing)
    EXPECT_EQ(id2, id1 + 1);
    EXPECT_EQ(id3, id2 + 1);
}

TEST_F(MsgIDTest, IDUniqueness) {
    const int count = 1000;
    uint64_t ids[count];
    
    // Generate many IDs
    for (int i = 0; i < count; i++) {
        ids[i] = uvrpc_msgid_next(ctx);
    }
    
    // Check all IDs are unique
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            EXPECT_NE(ids[i], ids[j]) << "Duplicate ID found: " << ids[i] << " at positions " << i << " and " << j;
        }
    }
}

TEST_F(MsgIDTest, IDNotZero) {
    // Message IDs should never be zero
    for (int i = 0; i < 100; i++) {
        uint64_t id = uvrpc_msgid_next(ctx);
        EXPECT_NE(id, 0) << "ID should not be zero, got: " << id;
    }
}

TEST_F(MsgIDTest, MultipleContexts) {
    // Create a second context
    uvrpc_msgid_ctx_t* ctx2 = uvrpc_msgid_ctx_new();
    ASSERT_NE(ctx2, nullptr);
    
    // IDs from different contexts should be independent
    uint64_t id1_ctx1 = uvrpc_msgid_next(ctx);
    uint64_t id1_ctx2 = uvrpc_msgid_next(ctx2);
    uint64_t id2_ctx1 = uvrpc_msgid_next(ctx);
    uint64_t id2_ctx2 = uvrpc_msgid_next(ctx2);
    
    // Each context should maintain its own sequence
    EXPECT_EQ(id2_ctx1, id1_ctx1 + 1);
    EXPECT_EQ(id2_ctx2, id1_ctx2 + 1);
    
    // IDs can overlap between contexts, that's okay
    // but each context should maintain sequential IDs
    
    uvrpc_msgid_ctx_free(ctx2);
}

TEST_F(MsgIDTest, HighThroughput) {
    const int count = 10000;
    
    // Generate many IDs quickly
    for (int i = 0; i < count; i++) {
        uint64_t id = uvrpc_msgid_next(ctx);
        EXPECT_NE(id, 0);
        EXPECT_GT(id, 0);
    }
}

TEST_F(MsgIDTest, ContextLifecycle) {
    // Create and destroy multiple contexts
    for (int i = 0; i < 10; i++) {
        uvrpc_msgid_ctx_t* temp_ctx = uvrpc_msgid_ctx_new();
        ASSERT_NE(temp_ctx, nullptr);
        
        // Generate some IDs
        uint64_t id1 = uvrpc_msgid_next(temp_ctx);
        uint64_t id2 = uvrpc_msgid_next(temp_ctx);
        EXPECT_EQ(id2, id1 + 1);
        
        uvrpc_msgid_ctx_free(temp_ctx);
    }
}

TEST_F(MsgIDTest, IDRange) {
    // Generate many IDs and check they stay within reasonable range
    const int count = 1000;
    uint64_t min_id = UINT64_MAX;
    uint64_t max_id = 0;
    
    for (int i = 0; i < count; i++) {
        uint64_t id = uvrpc_msgid_next(ctx);
        if (id < min_id) min_id = id;
        if (id > max_id) max_id = id;
    }
    
    // IDs should be sequential and within expected range
    EXPECT_EQ(max_id, min_id + count - 1);
}

TEST_F(MsgIDTest, ThreadSafetySimulated) {
    // Simulate thread safety by generating IDs in rapid succession
    const int iterations = 5000;
    uint64_t last_id = 0;
    
    for (int i = 0; i < iterations; i++) {
        uint64_t id = uvrpc_msgid_next(ctx);
        EXPECT_GT(id, last_id) << "IDs should be monotonically increasing";
        last_id = id;
    }
}