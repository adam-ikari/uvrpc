/**
 * UVRPC Gateway ID-Mapping Unit Tests
 *
 * uvrpc_idmap is what a gateway uses to map each client's msgid into a
 * space it can route back: gateway id -> (original msgid, client handle).
 * The functions are small and the edge cases are the whole point -- the
 * to_gateway returns 0 as its error sentinel, so valid ids start at 1 and a
 * caller that reads 0 as failure never discards a real mapping. Worth
 * pinning.
 */

#include <gtest/gtest.h>

extern "C" {
#include "../src/uvrpc_idmap.h"
#include "../include/uvrpc.h"  /* for uvrpc_alloc used by idmap */
}

class IdMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx = uvrpc_idmap_ctx_new();
        ASSERT_NE(ctx, nullptr);
    }
    void TearDown() override {
        if (ctx) {
            uvrpc_idmap_ctx_free(ctx);
            ctx = nullptr;
        }
    }
    uvrpc_idmap_ctx_t* ctx;
};

/* NULL ctx is the cheapest error path to reach, and the one a caller is
 * most likely to hit if it forgets to create the context. Each function
 * handles it without crashing. */
TEST(IdMapNullTest, NullContextIsSafe) {
    EXPECT_EQ(uvrpc_idmap_to_gateway(nullptr, 42, nullptr), 0u);

    uint32_t raw = 0;
    void* handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(nullptr, 1, &raw, &handle), -1);

    /* No crash, no return to check. */
    uvrpc_idmap_remove(nullptr, 1);
    SUCCEED();
}

TEST_F(IdMapTest, SequentialGatewayIds) {
    EXPECT_EQ(uvrpc_idmap_to_gateway(ctx, 100, nullptr), 1u);
    EXPECT_EQ(uvrpc_idmap_to_gateway(ctx, 101, nullptr), 2u);
    EXPECT_EQ(uvrpc_idmap_to_gateway(ctx, 102, nullptr), 3u);
}

TEST_F(IdMapTest, RoundTripPreservesOriginalIdAndHandle) {
    uint32_t raw = 7777;
    void* handle = reinterpret_cast<void*>(0x1234);
    uint32_t gateway = uvrpc_idmap_to_gateway(ctx, raw, handle);
    ASSERT_NE(gateway, 0u) << "0 is the error sentinel";

    uint32_t out_raw = 0;
    void* out_handle = nullptr;
    ASSERT_EQ(uvrpc_idmap_to_raw(ctx, gateway, &out_raw, &out_handle), 0);
    EXPECT_EQ(out_raw, raw);
    EXPECT_EQ(out_handle, handle);
}

/* to_gateway returns 0 as its error sentinel (NULL context). Ids therefore
 * start at 1, so a caller that reads 0 as failure never discards a valid
 * mapping. This pins both halves of that contract. */
TEST_F(IdMapTest, GatewayIdsStartAtOneSoZeroStaysASentinel) {
    uint32_t gateway = uvrpc_idmap_to_gateway(ctx, 5, nullptr);
    ASSERT_EQ(gateway, 1u);

    uint32_t out_raw = 0;
    void* out_handle = nullptr;
    ASSERT_EQ(uvrpc_idmap_to_raw(ctx, gateway, &out_raw, &out_handle), 0);
    EXPECT_EQ(out_raw, 5u);
}

TEST_F(IdMapTest, UnknownGatewayIdReturnsNotFound) {
    uint32_t out_raw = 0;
    void* out_handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, 999, &out_raw, &out_handle), -1);
}

TEST_F(IdMapTest, RemoveMakesLookupFail) {
    uint32_t gateway = uvrpc_idmap_to_gateway(ctx, 5, nullptr);
    uvrpc_idmap_remove(ctx, gateway);

    uint32_t out_raw = 0;
    void* out_handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, gateway, &out_raw, &out_handle), -1);
}

TEST_F(IdMapTest, RemoveUnknownIsSafe) {
    uvrpc_idmap_remove(ctx, 9999);  /* never existed */
    SUCCEED();

    /* And the table still works afterwards. */
    uint32_t g = uvrpc_idmap_to_gateway(ctx, 1, nullptr);
    uint32_t out_raw = 0;
    void* out_handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, g, &out_raw, &out_handle), 0);
}

TEST_F(IdMapTest, ManyClientsKeepDistinctHandles) {
    int a = 1, b = 2, c = 3;
    uint32_t g1 = uvrpc_idmap_to_gateway(ctx, 10, &a);
    uint32_t g2 = uvrpc_idmap_to_gateway(ctx, 11, &b);
    uint32_t g3 = uvrpc_idmap_to_gateway(ctx, 12, &c);

    void* h = nullptr;
    uint32_t raw = 0;
    ASSERT_EQ(uvrpc_idmap_to_raw(ctx, g2, &raw, &h), 0);
    EXPECT_EQ(raw, 11u);
    EXPECT_EQ(h, &b);

    ASSERT_EQ(uvrpc_idmap_to_raw(ctx, g3, &raw, &h), 0);
    EXPECT_EQ(raw, 12u);
    EXPECT_EQ(h, &c);

    ASSERT_EQ(uvrpc_idmap_to_raw(ctx, g1, &raw, &h), 0);
    EXPECT_EQ(raw, 10u);
    EXPECT_EQ(h, &a);
}

/* NULL out parameters on to_raw: a caller that only cares about the handle,
 * say, must still get an error rather than a dereference. */
TEST_F(IdMapTest, NullOutParamsAreRejected) {
    uint32_t gateway = uvrpc_idmap_to_gateway(ctx, 5, nullptr);

    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, gateway, nullptr, nullptr), -1);
    uint32_t raw = 0;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, gateway, &raw, nullptr), -1);
    void* handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, gateway, nullptr, &handle), -1);
}

/* Removing one entry does not disturb the others. */
TEST_F(IdMapTest, RemovingOneEntryKeepsOthers) {
    uint32_t g1 = uvrpc_idmap_to_gateway(ctx, 10, nullptr);
    uint32_t g2 = uvrpc_idmap_to_gateway(ctx, 11, nullptr);
    uint32_t g3 = uvrpc_idmap_to_gateway(ctx, 12, nullptr);

    uvrpc_idmap_remove(ctx, g2);

    uint32_t raw = 0;
    void* handle = nullptr;
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, g1, &raw, &handle), 0);
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, g2, &raw, &handle), -1);
    EXPECT_EQ(uvrpc_idmap_to_raw(ctx, g3, &raw, &handle), 0);
}
