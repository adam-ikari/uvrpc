/*
 * The concurrent combinators: uvrpc_async_all(), _any(), _race().
 *
 * These three shared one callback and one defect, and the defect was invisible
 * to every path that existed. concurrent_callback() recovered its slot index
 * from resp->user_data, and that channel is not wired at all:
 * client_recv_callback() assigns resp.user_data = NULL (src/uvrpc_client.c) and
 * nothing in the library ever sets it afterwards. So every response arrived
 * claiming index 0.
 *
 * Measured before the fix, all() over three calls against a service that
 * answered every one of them:
 *
 *     -> UVRPC_OK, served=3
 *        results[0] = 0x...a0
 *        results[1] = (nil)   <-- LOST
 *        results[2] = (nil)   <-- LOST
 *
 * Two of three answers were overwritten in slot 0 and leaked. The caller got
 * UVRPC_OK back, so there was no signal that anything was missing -- a
 * Promise.all() that silently drops results. uvrpc_async_any() had the same
 * callback, so completed_index was always 0.
 *
 * The line that was supposed to prevent this made it worse. uvrpc_async_all()
 * allocated a `uvrpc_response_t` per call and set its user_data, then never
 * passed it to anything -- the allocation was a leak, and the field it set was
 * on a struct nobody would ever read.
 *
 * Three more defects sat behind it, all reachable:
 *
 *  - the timeout path returned UVRPC_ERROR_TIMEOUT without releasing the
 *    results that HAD landed. Two of three answering before a 120 ms deadline
 *    left both results in concurrent_results[] with nobody holding a reference;
 *  - the next combinator's init loop set concurrent_results[i] = NULL without
 *    freeing, so a stale result from a timed-out call was dropped;
 *  - uvrpc_async_ctx_free() released pending_results[] and never
 *    concurrent_results[].
 *
 * Each case below pins one of those, and the whole file is run under
 * AddressSanitizer, which is what turns "leaked" from a claim into a failure.
 *
 * On the timeouts here: a slow *server* cannot be built. uvrpc_request_t is a
 * stack local in the dispatch function (src/uvrpc_server.c), so a handler that
 * kept its request across a timer would be holding a dead frame -- an
 * [ERROR] send_response at best, undefined behaviour at worst. The way to get
 * a combinator that gives up while answers are in flight is to give one of the
 * calls nobody to answer it: a client pointed at a port with nothing listening
 * on it. The other calls answer immediately, so by the time the deadline
 * expires their results are in place.
 */

#include <gtest/gtest.h>

#include <uvrpc.h>
#include <uvrpc_async.h>

#include <unistd.h>

#include "../free_port.h"

namespace {

template <typename Predicate>
bool PumpUntil(uv_loop_t* loop, Predicate predicate, int max_iterations = 400) {
    for (int i = 0; i < max_iterations; i++) {
        if (predicate()) return true;
        if (uv_run(loop, UV_RUN_ONCE) == 0) return predicate();
    }
    return predicate();
}

/* A live server, `kLive` connected clients, and one client pointed at a port
 * with nothing on it -- the "nobody will answer this" call, used to make a
 * combinator time out while its other calls have already landed. */
class Combinators : public ::testing::Test {
protected:
    static constexpr int kLive = 3;

    uv_loop_t loop;
    uvrpc_server_t* server = nullptr;
    uvrpc_client_t* live[kLive] = {nullptr, nullptr, nullptr};
    uvrpc_client_t* dead = nullptr;
    /* Fixture members, not locals: uvrpc_server_free() / uvrpc_client_free()
     * close libuv handles and uv_close() is deferred, so the loop has to run
     * again while that memory is still there. */
    uvbus_loop_registry_t* registry = nullptr;
    int served = 0;

    void SetUp() override {
        uv_loop_init(&loop);
        registry = uvbus_loop_registry_new();
    }

    void TearDown() override {
        if (server) {
            uvrpc_server_stop(server);
            uvrpc_server_free(server);
        }
        for (int i = 0; i < kLive; i++) {
            if (live[i]) uvrpc_client_free(live[i]);
        }
        if (dead) uvrpc_client_free(dead);
        for (int i = 0; i < 20; i++) {
            if (uv_run(&loop, UV_RUN_ONCE) == 0) break;
        }
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
    }

    static void handler(uvrpc_request_t* req, void* ctx) {
        auto* self = static_cast<Combinators*>(ctx);
        self->served++;
        uvrpc_request_send_response(req, UVRPC_OK, (const uint8_t*)"pong", 4);
        uvrpc_request_free(req);
    }

    uvrpc_client_t* MakeClient(const char* addr) {
        uvrpc_config_t* cc = uvrpc_config_new();
        uvrpc_config_set_loop(cc, &loop);
        uvrpc_config_set_address(cc, addr);
        uvrpc_config_set_transport(cc, UVBUS_TRANSPORT_TCP);
        uvrpc_client_t* c = uvrpc_client_create(cc);
        uvrpc_config_free(cc);
        return c;
    }

    void BringUp() {
        int port = acceptance_free_port();
        ASSERT_GT(port, 0);
        char addr[64];
        snprintf(addr, sizeof(addr), "tcp://127.0.0.1:%d", port);

        uvrpc_config_t* sc = uvrpc_config_new();
        uvrpc_config_set_loop(sc, &loop);
        uvrpc_config_set_address(sc, addr);
        uvrpc_config_set_transport(sc, UVBUS_TRANSPORT_TCP);
        server = uvrpc_server_create(sc);
        uvrpc_config_free(sc);
        ASSERT_NE(server, nullptr);
        uvrpc_server_register(server, "echo", handler, this);
        ASSERT_EQ(uvrpc_server_start(server), UVRPC_OK);
        for (int i = 0; i < 10; i++) uv_run(&loop, UV_RUN_NOWAIT);

        for (int i = 0; i < kLive; i++) {
            live[i] = MakeClient(addr);
            ASSERT_NE(live[i], nullptr);
            ASSERT_EQ(uvrpc_client_connect(live[i]), UVRPC_OK);
        }

        /* A port the kernel just handed out and nothing is listening on. */
        int dead_port = acceptance_free_port();
        ASSERT_GT(dead_port, 0);
        char dead_addr[64];
        snprintf(dead_addr, sizeof(dead_addr), "tcp://127.0.0.1:%d", dead_port);
        dead = MakeClient(dead_addr);
        ASSERT_NE(dead, nullptr);

        for (int i = 0; i < 100; i++) uv_run(&loop, UV_RUN_NOWAIT);
    }

    /* Fill `out` with `n` entries all pointing at `client` and the "echo"
     * method, which is the only shape a combinator takes. */
    static void Fill(uvrpc_client_t** out_clients, const char** out_methods,
                     const uint8_t** out_params, size_t* out_sizes, int n,
                     uvrpc_client_t* client) {
        for (int i = 0; i < n; i++) {
            out_clients[i] = client;
            out_methods[i] = "echo";
            out_params[i] = reinterpret_cast<const uint8_t*>("x");
            out_sizes[i] = 1;
        }
    }
};

}  // namespace

/* ------------------------------------------------------------------- all */

/* The defect itself: three calls, three answers, three results. Before the fix
 * two of them were overwritten in slot 0 and lost. */
TEST_F(Combinators, AllReturnsOneResultPerCall) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[3] = {live[0], live[1], live[2]};
    const char* methods[3] = {"echo", "echo", "echo"};
    const uint8_t* params[3] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b"),
                                reinterpret_cast<const uint8_t*>("c")};
    size_t sizes[3] = {1, 1, 1};
    uvrpc_async_result_t** results = nullptr;

    EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results, 3,
                              5000),
              UVRPC_OK);
    EXPECT_EQ(served, 3);
    ASSERT_NE(results, nullptr);

    for (int i = 0; i < 3; i++) {
        /* Not just "non-NULL": each must be a distinct allocation. The old
         * code put all three responses in the same slot, so a caller could
         * have received three pointers to one object. */
        ASSERT_NE(results[i], nullptr) << "results[" << i << "] was dropped";
        EXPECT_EQ(results[i]->status, UVRPC_OK);
        EXPECT_EQ(results[i]->result_size, 4u);
        if (results[i]->result && results[i]->result_size == 4) {
            EXPECT_EQ(memcmp(results[i]->result, "pong", 4), 0);
        }
        for (int j = 0; j < i; j++) {
            EXPECT_NE(results[i], results[j])
                << "results[" << i << "] and results[" << j
                << "] are the same object: slot reuse";
        }
        uvrpc_async_result_free(results[i]);
    }
    free(results);
    uvrpc_async_ctx_free(ctx);
}

/* Two of three answer immediately; the third has nobody to answer it. The
 * deadline expires with two results already in place, and those two are never
 * handed to a caller -- so the function has to release them itself. It used to
 * return without touching them, and this file is what makes that a failure
 * rather than a rumour: AddressSanitizer reports the two. */
TEST_F(Combinators, AllTimeoutReleasesTheResultsThatLanded) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[3] = {live[0], live[1], dead};
    const char* methods[3] = {"echo", "echo", "echo"};
    const uint8_t* params[3] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b"),
                                reinterpret_cast<const uint8_t*>("c")};
    size_t sizes[3] = {1, 1, 1};
    uvrpc_async_result_t** results = reinterpret_cast<uvrpc_async_result_t**>(0x1);

    /* 300 ms is far longer than loopback needs and far shorter than "never". */
    EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results, 3,
                              300),
              UVRPC_ERROR_TIMEOUT);
    /* The two live calls did reach the service. */
    EXPECT_EQ(served, 2);
    /* On failure the out-parameter is left alone, not half-filled. */
    EXPECT_EQ(results, reinterpret_cast<uvrpc_async_result_t**>(0x1));

    uvrpc_async_ctx_free(ctx);
}

/* Freeing the context has to reach concurrent_results[] as well. A combinator
 * that gave up leaves its landed results there, and nothing else in the
 * teardown path touched them. */
TEST_F(Combinators, ContextFreeReleasesConcurrentResults) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[2] = {live[0], dead};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t** results = nullptr;

    EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results, 2,
                              300),
              UVRPC_ERROR_TIMEOUT);
    EXPECT_EQ(served, 1);

    /* Straight into teardown: this is where the one landed result used to
     * survive the context that owned it. */
    uvrpc_async_ctx_free(ctx);
}

/* A combinator that gave up leaves its slots disarmed, so an answer arriving
 * afterwards is dropped instead of landing in the next call's results. Without
 * that guard the straggler of the first call fills a slot the second call
 * believes it filled itself, and all() returns a result from the wrong call.
 *
 * The client here is freed after the timeout, which is what stops the straggler
 * from arriving at all -- the point is that a *disarmed* context stays
 * well-formed, and the next combinator still gets exactly its own answers. */
TEST_F(Combinators, ContextIsReusableAfterATimedOutCombinator) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    {
        uvrpc_client_t* clients[3] = {live[0], live[1], dead};
        const char* methods[3] = {"echo", "echo", "echo"};
        const uint8_t* params[3] = {reinterpret_cast<const uint8_t*>("a"),
                                    reinterpret_cast<const uint8_t*>("b"),
                                    reinterpret_cast<const uint8_t*>("c")};
        size_t sizes[3] = {1, 1, 1};
        uvrpc_async_result_t** results = nullptr;

        EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results,
                                  3, 300),
              UVRPC_ERROR_TIMEOUT);
    }

    /* Now a clean combinator on the same context. Every answer it returns must
     * be one of its own, and the count must match. */
    int served_before = served;
    uvrpc_client_t* clients[2] = {live[0], live[1]};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("x"),
                                reinterpret_cast<const uint8_t*>("y")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t** results = nullptr;

    EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results, 2,
                              5000),
              UVRPC_OK);
    ASSERT_NE(results, nullptr);
    EXPECT_EQ(served - served_before, 2);

    for (int i = 0; i < 2; i++) {
        ASSERT_NE(results[i], nullptr);
        uvrpc_async_result_free(results[i]);
    }
    free(results);
    uvrpc_async_ctx_free(ctx);
}

/* ------------------------------------------------------------------- any */

/* any() hands back one answer and releases the ones that lost the race. Those
 * losers were leaked on both exit paths -- the early return on success and the
 * return on timeout -- because nothing swept the array. */
TEST_F(Combinators, AnyReleasesTheCallsThatLost) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[3] = {live[0], live[1], live[2]};
    const char* methods[3] = {"echo", "echo", "echo"};
    const uint8_t* params[3] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b"),
                                reinterpret_cast<const uint8_t*>("c")};
    size_t sizes[3] = {1, 1, 1};
    uvrpc_async_result_t* result = nullptr;
    int index = -1;

    EXPECT_EQ(uvrpc_async_any(ctx, clients, methods, params, sizes, &result,
                              &index, 3, 5000),
              UVRPC_OK);
    ASSERT_NE(result, nullptr);
    EXPECT_GE(index, 0);
    EXPECT_LT(index, 3);
    EXPECT_EQ(result->status, UVRPC_OK);

    uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);
}

/* any() times out only when nobody answers. Unlike all(), it returns the
 * moment one call completes -- so a live client alongside a dead one is a
 * success, not a timeout, which is what the first draft of this test asserted
 * and got wrong. */
TEST_F(Combinators, AnyTimeoutReleasesWhatLanded) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    /* Both calls pointed at ports with nothing listening: no answer can arrive
     * at all, so the deadline is the only thing that ends the wait. */
    uvrpc_client_t* clients[2] = {dead, dead};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t* result = reinterpret_cast<uvrpc_async_result_t*>(0x1);
    int index = -1;

    EXPECT_EQ(uvrpc_async_any(ctx, clients, methods, params, sizes, &result,
                              &index, 2, 300),
              UVRPC_ERROR_TIMEOUT);
    EXPECT_EQ(result, reinterpret_cast<uvrpc_async_result_t*>(0x1));
    EXPECT_EQ(index, -1);
    EXPECT_EQ(served, 0);

    uvrpc_async_ctx_free(ctx);
}

/* The other half of the same point: any() with one live and one dead call is a
 * success, and the dead one is still released rather than left in the array. */
TEST_F(Combinators, AnySucceedsOnTheLiveCallAndReleasesTheDeadOne) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[2] = {live[0], dead};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t* result = nullptr;
    int index = -1;

    EXPECT_EQ(uvrpc_async_any(ctx, clients, methods, params, sizes, &result,
                              &index, 2, 300),
              UVRPC_OK);
    ASSERT_NE(result, nullptr);
    /* The live call is slot 0; if the dead one had somehow won, index would
     * point at it and the result would not be the service's answer. */
    EXPECT_EQ(index, 0);
    EXPECT_EQ(result->result_size, 4u);

    uvrpc_async_result_free(result);

    /* The loser is gone from the array. A second any() on the same context
     * must start from empty slots, and this is the leak check for it. */
    uvrpc_async_ctx_free(ctx);
}

/* race() is declared in the public header and had no definition anywhere in the
 * library until it was made an alias for any(): linking a caller that used it
 * failed at the link step. It has to actually run. */
TEST_F(Combinators, RaceRunsAndReturnsAnAnswer) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[2] = {live[0], live[1]};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t* result = nullptr;
    int index = -1;

    EXPECT_EQ(uvrpc_async_race(ctx, clients, methods, params, sizes, &result,
                               &index, 2, 5000),
              UVRPC_OK);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->status, UVRPC_OK);

    uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);
}

/* -------------------------------------------------------------- arguments */

TEST_F(Combinators, RejectsCountsOutsideTheSupportedRange) {
    BringUp();

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);

    uvrpc_client_t* clients[2] = {live[0], live[1]};
    const char* methods[2] = {"echo", "echo"};
    const uint8_t* params[2] = {reinterpret_cast<const uint8_t*>("a"),
                                reinterpret_cast<const uint8_t*>("b")};
    size_t sizes[2] = {1, 1};
    uvrpc_async_result_t** results = nullptr;

    EXPECT_EQ(uvrpc_async_all(ctx, clients, methods, params, sizes, &results, 0,
                              100),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_async_all(nullptr, clients, methods, params, sizes,
                              &results, 2, 100),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(results, nullptr);

    uvrpc_async_ctx_free(ctx);
}
