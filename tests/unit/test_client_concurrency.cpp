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
            uv_run(&loop, UV_RUN_DEFAULT);
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
/* --- Live quota check ---------------------------------------------------- */

/* TCP is the only in-process transport whose responses are asynchronous, so it
 * is the only one that can hold callback slots open long enough to observe the
 * quota. INPROC/SAMELOOP complete the round trip inside the call. */
#define QUOTA_TEST_ADDRESS "tcp://127.0.0.1:5573"

static void quota_ping_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    static const uint8_t payload = 7;
    uvrpc_request_send_response(req, UVRPC_OK, &payload, sizeof(payload));
}

static void quota_noop_response(uvrpc_response_t* resp, void* ctx) {
    (void)resp;
    (void)ctx;
}

static void quota_mark_connected(int status, void* ctx) {
    *(int*)ctx = (status == 0) ? 1 : -1;
}

TEST(UVRPCQuotaLiveTest, MaxConcurrentGatesSingleCalls) {
    uv_loop_t loop;
    ASSERT_EQ(uv_loop_init(&loop), 0);

    uvrpc_config_t* server_config = uvrpc_config_new();
    uvrpc_config_set_loop(server_config, &loop);
    uvrpc_config_set_address(server_config, QUOTA_TEST_ADDRESS);
    uvrpc_server_t* server = uvrpc_server_create(server_config);
    ASSERT_NE(server, nullptr);
    uvrpc_server_register(server, "ping", quota_ping_handler, nullptr);
    ASSERT_EQ(uvrpc_server_start(server), UVRPC_OK);

    const int quota = 5;
    uvrpc_config_t* client_config = uvrpc_config_new();
    uvrpc_config_set_loop(client_config, &loop);
    uvrpc_config_set_address(client_config, QUOTA_TEST_ADDRESS);
    uvrpc_config_set_max_concurrent(client_config, quota);
    uvrpc_client_t* client = uvrpc_client_create(client_config);
    ASSERT_NE(client, nullptr);

    int connected = 0;
    ASSERT_EQ(uvrpc_client_connect_with_callback(client, quota_mark_connected, &connected), UVRPC_OK);
    for (int i = 0; i < 200 && connected == 0; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    ASSERT_EQ(connected, 1);

    /* Nothing pumps the loop in between, so each accepted call keeps its slot. */
    int accepted = 0;
    int first_reject = UVRPC_OK;
    for (int i = 0; i < quota * 4; i++) {
        int ret = uvrpc_client_call(client, "ping", nullptr, 0, quota_noop_response, nullptr);
        if (ret == UVRPC_OK) {
            accepted++;
        } else {
            first_reject = ret;
            break;
        }
    }

    EXPECT_EQ(accepted, quota);
    EXPECT_EQ(first_reject, UVRPC_ERROR_RATE_LIMITED);
    EXPECT_EQ(uvrpc_client_get_pending_count(client), quota);

    /* Draining releases the slots; the counter must land on zero, not below. */
    for (int i = 0; i < 500 && uvrpc_client_get_pending_count(client) > 0; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    EXPECT_EQ(uvrpc_client_get_pending_count(client), 0);

    /* And the quota is usable again once the in-flight set is empty. */
    EXPECT_EQ(uvrpc_client_call(client, "ping", nullptr, 0, quota_noop_response, nullptr), UVRPC_OK);
    for (int i = 0; i < 500 && uvrpc_client_get_pending_count(client) > 0; i++) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    EXPECT_EQ(uvrpc_client_get_pending_count(client), 0);

    uvrpc_client_free(client);
    uvrpc_server_stop(server);
    uvrpc_server_free(server);
    uvrpc_config_free(client_config);
    uvrpc_config_free(server_config);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);
}
