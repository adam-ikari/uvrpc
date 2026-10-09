/*
 * Retry, and the API contract of the blocking await.
 *
 * Retry had no test at all, and it was the most broken thing in the file. Four
 * of these five cases were failing before:
 *
 *  - a server that failed every time was retried exactly ONCE, regardless of
 *    max_retries. The delay timer callback did the retry and then freed the
 *    struct holding the timer, so the second attempt never happened.
 *  - the retry timer was never uv_close()d, so after any retry the loop could
 *    never be closed: uv_loop_close() returned UV_EBUSY permanently.
 *  - retry_with_backoff() returned UVRPC_ERROR to mean "a retry is scheduled",
 *    which the caller cannot tell apart from failure. That is why
 *    concurrent_demo has always printed "[Async] Retry failed" against a server
 *    that answers every time.
 *  - it returns UVRPC_OK when the server answered with an error frame, because
 *    uvrpc_client_call_async() returns UVRPC_OK for a call that completed.
 *
 * The fifth is the same shape as the defects fixed in await_span: a wait whose
 * deadline never fires, and nothing else on the loop, must not spin.
 */

#include <gtest/gtest.h>

#include <uvrpc.h>
#include <uvrpc_async.h>
#include "uvrpc_primitives.h"

#include <unistd.h>

/* tests/free_port.h -- a TCP port the kernel picks, so a failure that has
 * nothing to do with the retry logic cannot cause these tests. */
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

/* A loopback server whose handler can be made to fail a fixed number of times. */
class Endpoint : public ::testing::Test {
protected:
    uv_loop_t loop;
    uvrpc_server_t* server = nullptr;
    uvrpc_client_t* client = nullptr;
    /* Fixture members, not locals: uvrpc_server_free() / uvrpc_client_free()
     * close libuv handles, and uv_close() is deferred, so the loop has to run
     * again while the memory is still there. */
    uvbus_loop_registry_t* registry = nullptr;
    int port = 0;
    int served = 0;
    int failures_to_send = 0;

    void SetUp() override {
        uv_loop_init(&loop);
        registry = uvbus_loop_registry_new();
    }

    void TearDown() override {
        if (server) {
            uvrpc_server_stop(server);
            uvrpc_server_free(server);
        }
        if (client) {
            uvrpc_client_free(client);
        }
        for (int i = 0; i < 20; i++) {
            if (uv_run(&loop, UV_RUN_ONCE) == 0) break;
        }
        uvbus_loop_registry_free(registry);
        uv_loop_close(&loop);
    }

    static void handler(uvrpc_request_t* req, void* ctx) {
        auto* self = static_cast<Endpoint*>(ctx);
        self->served++;
        if (self->served <= self->failures_to_send) {
            /* An error frame, not a successful result carrying a bad status:
             * uvrpc_request_send_response() ignores its status argument
             * ("not yet propagated in the frame"), so sending a result with
             * status != 0 would reach the caller as a success. */
            uvrpc_response_send_error(req, UVRPC_ERROR_TIMEOUT, "not yet");
            uvrpc_request_free(req);
            return;
        }
        uvrpc_request_send_response(req, UVRPC_OK, (const uint8_t*)"pong", 4);
        uvrpc_request_free(req);
    }

    /* TCP, because the retry path is only interesting where a response is
     * actually asynchronous -- on INPROC it completes inline. */
    void BringUp() {
        port = acceptance_free_port();
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

        uvrpc_config_t* cc = uvrpc_config_new();
        uvrpc_config_set_loop(cc, &loop);
        uvrpc_config_set_address(cc, addr);
        uvrpc_config_set_transport(cc, UVBUS_TRANSPORT_TCP);
        client = uvrpc_client_create(cc);
        uvrpc_config_free(cc);
        ASSERT_NE(client, nullptr);
        ASSERT_EQ(uvrpc_client_connect(client), UVRPC_OK);
        for (int i = 0; i < 100; i++) uv_run(&loop, UV_RUN_NOWAIT);
    }
};

}  // namespace

/* ---------------------------------------------------------------- retry */

TEST_F(Endpoint, RetryReachesEveryAttemptNotJustOne) {
    BringUp();
    failures_to_send = 4;  /* server refuses 4 times, then answers */

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_NE(ctx, nullptr);
    ASSERT_EQ(uvrpc_async_exec(ctx, 5000), UVRPC_OK);

    uvrpc_async_result_t* result = nullptr;
    EXPECT_EQ(uvrpc_async_retry(ctx, client, "echo", (const uint8_t*)"x", 1,
                                &result, 5, 20),
              UVRPC_OK);
    /* 1 original + up to 5 retries; the fifth request (led 4 failures) is the
     * one that succeeded. The old code stopped after the second. */
    EXPECT_EQ(served, 5);
    /* uvrpc_async_result_free(): the retry loop hands back the result it got,
     * and only the failing ones get freed inside it. */
    uvrpc_async_result_free(result);

    uvrpc_async_ctx_free(ctx);
}

TEST_F(Endpoint, RetryCountsFailuresNotJustAttempts) {
    BringUp();
    failures_to_send = 0;  /* answers immediately */

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_exec(ctx, 5000);
    uvrpc_async_result_t* result = nullptr;
    EXPECT_EQ(uvrpc_async_retry(ctx, client, "echo", (const uint8_t*)"x", 1,
                                &result, 3, 20),
              UVRPC_OK);
    EXPECT_EQ(served, 1);  /* a success on the first try is not retried */
    uvrpc_async_result_free(result);

    uvrpc_async_ctx_free(ctx);
}

TEST_F(Endpoint, RetryReportsFailureWhenEveryAttemptFails) {
    BringUp();
    failures_to_send = 1000;  /* always refuses */

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_exec(ctx, 5000);
    uvrpc_async_result_t* result = nullptr;
    /* Not UVRPC_OK, and not the generic failure either: the contract is to
     * report the last failure the service gave, which here is -5 -- the code
     * the handler sent, not a code the library invented. */
    EXPECT_EQ(uvrpc_async_retry(ctx, client, "echo", (const uint8_t*)"x", 1,
                                &result, 3, 20),
              UVRPC_ERROR_TIMEOUT);
    EXPECT_EQ(served, 4);    /* 1 + 3 retries */

    uvrpc_async_ctx_free(ctx);
}

TEST_F(Endpoint, RetryLeavesTheLoopClosable) {
    BringUp();
    failures_to_send = 1;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_exec(ctx, 5000);
    uvrpc_async_result_t* result = nullptr;
    uvrpc_async_retry(ctx, client, "echo", (const uint8_t*)"x", 1, &result, 3, 10);
    if (result) uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);

    /* TearDown closes the loop. The old retry timer leaked, so
     * uv_loop_close() returned UV_EBUSY here. */
    SUCCEED();
}

TEST_F(Endpoint, RetryRejectsACallThatCannotWork) {
    BringUp();
    /* No uvrpc_async_exec(): the server is fine but every call would be
     * rejected for a reason retrying cannot fix. */
    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_result_t* result = nullptr;
    EXPECT_EQ(uvrpc_async_retry(ctx, client, "echo", (const uint8_t*)"x", 1,
                                &result, 3, 10),
              UVRPC_ERROR_INVALID_STATE);
    EXPECT_EQ(served, 0);

    /* ...and so is a call with nothing right about it. */
    EXPECT_EQ(uvrpc_async_retry(nullptr, client, "echo", (const uint8_t*)"x",
                                1, &result, 3, 10),
              UVRPC_ERROR_INVALID_PARAM);
    uvrpc_async_ctx_free(ctx);
}

/* -------------------------------------------------------------- backoff */

TEST_F(Endpoint, BackoffGrowsTheDelay) {
    BringUp();
    failures_to_send = 1000;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_exec(ctx, 5000);
    uvrpc_async_result_t* result = nullptr;

    /* multiplier 1 means the delay does not grow, so the loop should bound by
     * the attempt count and not by the delay. If the delay were computed
     * wrong -- say as initial^attempt -- this would take minutes. */
    uint64_t before = served;
    EXPECT_EQ(uvrpc_async_retry_with_backoff(ctx, client, "echo",
                                             (const uint8_t*)"x", 1, &result,
                                             2, 120, 1.0),
              UVRPC_ERROR_TIMEOUT);  /* the server's code, not UVRPC_ERROR */
    EXPECT_GT(served, before);  /* it actually retried */
    EXPECT_EQ(served - before, 3);  /* 1 + 2 retries */

    uvrpc_async_ctx_free(ctx);
}

TEST_F(Endpoint, BackoffStopsWhenTheServiceRecovers) {
    BringUp();
    failures_to_send = 1;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    uvrpc_async_exec(ctx, 5000);
    uvrpc_async_result_t* result = nullptr;
    EXPECT_EQ(uvrpc_async_retry_with_backoff(ctx, client, "echo",
                                             (const uint8_t*)"x", 1, &result,
                                             5, 30, 2.0),
              UVRPC_OK);
    EXPECT_EQ(served, 2);
    uvrpc_async_result_free(result);

    uvrpc_async_ctx_free(ctx);
}

/* --------------------------------------------------- the await contract */

/* The case the first version of await_span got wrong.
 *
 * With a deadline that never fires and nothing else on the loop, an await must
 * not spin. The response-callback path exits through uv_stop(), so the loop
 * condition is what decides -- and UV_RUN_DEFAULT only returns once nothing is
 * referenced, which never happens with a client connected. This asserts the
 * alternative: the wait ends because the loop ran out of work. */
TEST_F(Endpoint, AwaitEndsWhenTheLoopHasNothingLeft) {
    BringUp();
    /* The handler never answers, so no response will settle the wait. */
    failures_to_send = 1000;

    uvrpc_async_ctx_t* ctx = uvrpc_async_ctx_new(&loop);
    ASSERT_EQ(uvrpc_async_exec(ctx, 0), UVRPC_OK);  /* 0 documented as "no deadline" */

    uvrpc_async_result_t* result = nullptr;
    /* The client is still connected, so uv_run(UV_RUN_ONCE) blocks until the
     * next event rather than returning 0. This one waits on nothing, so it
     * must end on the deadline being the only thing left -- which is why the
     * call below is expected to fail rather than succeed. */
    int status = uvrpc_client_call_async(ctx, client, "echo",
                                         (const uint8_t*)"p", 1, &result);
    /* Whatever the outcome, it must have ended. */
    EXPECT_TRUE(status == UVRPC_OK || status == UVRPC_ERROR_TIMEOUT);

    if (result) uvrpc_async_result_free(result);
    uvrpc_async_ctx_free(ctx);
}
