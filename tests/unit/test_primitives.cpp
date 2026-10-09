/**
 * UVRPC async primitives: promise, semaphore, waitgroup.
 *
 * This file exists because src/uvrpc_primitives.c had no test at all. It is
 * 1000 lines of public API -- exported from include/uvrpc_primitives.h,
 * documented with worked examples, exercised only by the demos in examples/ --
 * and the demos are not assertions. Four defects lived there, all of the kind
 * that a demo cannot catch because a demo is not asked to prove anything:
 *
 *  - uvrpc_promise_wait() returned UVRPC_OK for a promise that never settled,
 *    because a pending promise has no error code and 0 is UVRPC_OK. The
 *    documented call pattern then reads a result that does not exist.
 *  - uvrpc_promise_wait() used uv_run(UV_RUN_DEFAULT), which does not return
 *    while anything on the loop is referenced. A connected client is enough,
 *    so waiting for a response over TCP hung forever.
 *  - uvrpc_promise_get_result() left its out-parameters untouched on failure,
 *    and the documented example ignores its return value.
 *  - uvrpc_promise_race() freed its context from the first callback, so every
 *    loser that settled afterwards read freed memory.
 *
 * The loop-pumping helpers below use UV_RUN_ONCE for the reason the library
 * code now does: UV_RUN_DEFAULT only returns once nothing is referenced.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "uvrpc.h"
#include "uvrpc_primitives.h"

namespace {

/* Pump until `predicate` holds or we give up. Bounded on purpose: a test that
 * can wait forever is a way to burn CI minutes, not a test. */
template <typename Predicate>
bool PumpUntil(uv_loop_t* loop, Predicate predicate, int max_iterations = 200) {
    for (int i = 0; i < max_iterations; i++) {
        if (predicate()) return true;
        if (uv_run(loop, UV_RUN_ONCE) == 0) return predicate();
    }
    return predicate();
}

/* Resolves the promise after `delay_ms`, from the loop.
 *
 * The timer handle is closed inside its own callback, and uv_close() only
 * queues the close -- the handle memory has to stay valid until the loop runs
 * the close callback. A stack-allocated struct therefore has to outlive the
 * next loop iteration, which is why this is a fixture member rather than a
 * local: a local crashed the first time the test ran. */
struct DelayedResolve {
    uvrpc_promise_t* promise;
    uv_timer_t timer;
    const uint8_t* payload;
    size_t size;

    static void fire(uv_timer_t* t) {
        auto* self = static_cast<DelayedResolve*>(t->data);
        uvrpc_promise_resolve(self->promise, self->payload, self->size);
        uv_close((uv_handle_t*)&self->timer, nullptr);
    }
};

class Promises : public ::testing::Test {
protected:
    uv_loop_t loop;
    /* Fixture members, not locals: see DelayedResolve. The semaphore and the
     * waitgroup are here for the same reason -- their cleanup functions embed
     * a uv_async_t and close it, and that close is deferred to the next loop
     * iteration, by which time a local would be gone. */
    DelayedResolve delayed;
    uv_timer_t scratch;
    bool scratch_armed = false;
    uvrpc_semaphore_t semaphore;
    bool semaphore_armed = false;
    uvrpc_waitgroup_t waitgroup;
    bool waitgroup_armed = false;
    /* The promise the combinators fold into. Also a member: cleanup() closes
     * the embedded uv_async_t, and that close runs on a later iteration. */
    uvrpc_promise_t combined;
    bool combined_armed = false;

    void SetUp() override { uv_loop_init(&loop); }

    void TearDown() override {
        StopUnrelatedHandle();
        if (semaphore_armed) uvrpc_semaphore_cleanup(&semaphore);
        if (waitgroup_armed) uvrpc_waitgroup_cleanup(&waitgroup);
        if (combined_armed) uvrpc_promise_cleanup(&combined);
        /* Drain until the loop is quiet, so every handle closed above has
         * actually been closed before the fixture member it lives in goes
         * away. uv_close() only queues the work. */
        for (int i = 0; i < 20; i++) {
            if (uv_run(&loop, UV_RUN_ONCE) == 0) break;
        }
        uv_loop_close(&loop);
    }

    void StartDelayedResolve(uvrpc_promise_t* promise,
                             const uint8_t* payload, size_t size,
                             int delay_ms) {
        delayed.promise = promise;
        delayed.payload = payload;
        delayed.size = size;
        uv_timer_init(&loop, &delayed.timer);
        delayed.timer.data = &delayed;
        uv_timer_start(&delayed.timer, DelayedResolve::fire, delay_ms, 0);
    }

    /* A handle on the loop that nothing else depends on, for asserting what a
     * wait does when it cannot be satisfied. Fixture member for the same
     * reason as DelayedResolve: uv_close() is deferred, so a local would be
     * dead memory by the time the loop ran the close. */
    void StartUnrelatedHandle(int delay_ms) {
        uv_timer_init(&loop, &scratch);
        scratch_armed = true;
        uv_timer_start(&scratch, [](uv_timer_t*) {}, delay_ms, 0);
    }

    void StopUnrelatedHandle() {
        if (scratch_armed) {
            uv_timer_stop(&scratch);
            uv_close((uv_handle_t*)&scratch, nullptr);
            scratch_armed = false;
        }
    }

    void InitSemaphore(int permits) {
        EXPECT_EQ(uvrpc_semaphore_init(&semaphore, &loop, permits), UVRPC_OK);
        semaphore_armed = true;
    }

    void InitWaitGroup() {
        EXPECT_EQ(uvrpc_waitgroup_init(&waitgroup, &loop), UVRPC_OK);
        waitgroup_armed = true;
    }

    void InitCombined() {
        EXPECT_EQ(uvrpc_promise_init(&combined, &loop), UVRPC_OK);
        combined_armed = true;
    }

    void FinishCombined() {
        if (combined_armed) {
            uvrpc_promise_cleanup(&combined);
            combined_armed = false;
        }
    }
};

}  // namespace

/* ---------------------------------------------------------------- wait() */

TEST_F(Promises, WaitOnPromiseThatNeverSettlesReportsFailure) {
    auto* promise = uvrpc_promise_create(&loop);
    ASSERT_NE(promise, nullptr);

    EXPECT_EQ(uvrpc_promise_is_pending(promise), 1);

    /* Nothing on the loop can settle this, so wait() gives up. It must not
     * report success: UVRPC_OK is 0, and a pending promise's error_code is
     * also 0, so "return error_code" returns success for a promise that never
     * resolved. */
    const int ret = uvrpc_promise_wait(promise);
    EXPECT_NE(ret, UVRPC_OK) << "wait() reported success for an unsettled promise";
    EXPECT_EQ(ret, UVRPC_ERROR_TIMEOUT);
    EXPECT_EQ(uvrpc_promise_is_pending(promise), 1);

    uvrpc_promise_destroy(promise);
}

TEST_F(Promises, WaitReturnsResultOfAFulfilledPromise) {
    auto* promise = uvrpc_promise_create(&loop);
    ASSERT_NE(promise, nullptr);

    const uint8_t payload[] = {1, 2, 3, 4};
    StartDelayedResolve(promise, payload, sizeof(payload), 10);

    EXPECT_EQ(uvrpc_promise_wait(promise), UVRPC_OK);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(promise), 1);

    uint8_t* result = nullptr;
    size_t size = 0;
    ASSERT_EQ(uvrpc_promise_get_result(promise, &result, &size), UVRPC_OK);
    EXPECT_EQ(size, sizeof(payload));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, payload, sizeof(payload)), 0);

    uvrpc_promise_destroy(promise);
}

TEST_F(Promises, WaitReturnsTheRejectionCode) {
    auto* promise = uvrpc_promise_create(&loop);
    ASSERT_NE(promise, nullptr);

    uvrpc_promise_reject(promise, UVRPC_ERROR_NOT_FOUND, "no such thing");

    EXPECT_EQ(uvrpc_promise_wait(promise), UVRPC_ERROR_NOT_FOUND);
    EXPECT_EQ(uvrpc_promise_is_rejected(promise), 1);
    EXPECT_STREQ(uvrpc_promise_get_error(promise), "no such thing");
    EXPECT_EQ(uvrpc_promise_get_error_code(promise), UVRPC_ERROR_NOT_FOUND);

    uvrpc_promise_destroy(promise);
}

TEST_F(Promises, WaitGivesUpWhenNothingCanSettleThePromise) {
    auto* promise = uvrpc_promise_create(&loop);

    /* A handle on the loop that the promise does not depend on -- the shape of
     * a connected client. Under UV_RUN_DEFAULT this is what made wait() hang:
     * DEFAULT returns only once nothing is referenced any more, so it never
     * got far enough to re-check the promise's state. The timer here is
     * one-shot and unrelated, so after it fires the loop has nothing left and
     * wait() has to give up rather than spin. */
    StartUnrelatedHandle(10);

    EXPECT_EQ(uvrpc_promise_wait(promise), UVRPC_ERROR_TIMEOUT);

    uvrpc_promise_destroy(promise);
}

/* --------------------------------------------------------- get_result() */

TEST_F(Promises, GetResultClearsOutputsWhenItFails) {
    auto* promise = uvrpc_promise_create(&loop);

    /* Poison the outputs the way an uninitialised stack would, then ask for a
     * result that does not exist. */
    uint8_t* result = reinterpret_cast<uint8_t*>(0xdeadbeef);
    size_t size = 12345;

    EXPECT_EQ(uvrpc_promise_get_result(promise, &result, &size),
              UVRPC_ERROR_INVALID_STATE);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(size, 0u);

    uvrpc_promise_destroy(promise);
}

TEST_F(Promises, GetResultClearsOutputsForANullPromise) {
    uint8_t* result = reinterpret_cast<uint8_t*>(0xdeadbeef);
    size_t size = 12345;

    EXPECT_EQ(uvrpc_promise_get_result(nullptr, &result, &size),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(size, 0u);
}

/* -------------------------------------------------------------- states */

TEST_F(Promises, StateTransitions) {
    auto* promise = uvrpc_promise_create(&loop);
    ASSERT_NE(promise, nullptr);
    EXPECT_EQ(uvrpc_promise_is_pending(promise), 1);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(promise), 0);
    EXPECT_EQ(uvrpc_promise_is_rejected(promise), 0);

    EXPECT_EQ(uvrpc_promise_resolve(promise, (const uint8_t*)"x", 1), UVRPC_OK);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(promise), 1);
    EXPECT_EQ(uvrpc_promise_is_pending(promise), 0);

    /* Settling twice is a state error, not a silent overwrite. */
    EXPECT_EQ(uvrpc_promise_resolve(promise, (const uint8_t*)"y", 1),
              UVRPC_ERROR_INVALID_STATE);
    EXPECT_EQ(uvrpc_promise_reject(promise, -1, "late"),
              UVRPC_ERROR_INVALID_STATE);

    uvrpc_promise_destroy(promise);
}

TEST_F(Promises, NullArgumentsAreRejected) {
    uint8_t* result = nullptr;
    size_t size = 0;
    EXPECT_EQ(uvrpc_promise_resolve(nullptr, (const uint8_t*)"x", 1),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_reject(nullptr, -1, "x"), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_wait(nullptr), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_create(nullptr), nullptr);
    /* cleanup and destroy tolerate NULL */
    uvrpc_promise_cleanup(nullptr);
    uvrpc_promise_destroy(nullptr);
    EXPECT_EQ(uvrpc_promise_is_pending(nullptr), 0);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(nullptr), 0);
    EXPECT_EQ(uvrpc_promise_is_rejected(nullptr), 0);
    EXPECT_EQ(uvrpc_promise_get_error(nullptr), nullptr);
    EXPECT_EQ(uvrpc_promise_get_error_code(nullptr), 0);
    EXPECT_EQ(uvrpc_promise_get_result(nullptr, &result, &size),
              UVRPC_ERROR_INVALID_PARAM);
}

/* ----------------------------------------------------------- combinators */

TEST_F(Promises, RaceForwardsTheFirstResultAndSurvivesTheRest) {
    auto* a = uvrpc_promise_create(&loop);
    auto* b = uvrpc_promise_create(&loop);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    InitCombined();

    uvrpc_promise_t* promises[] = {a, b};
    ASSERT_EQ(uvrpc_promise_race(promises, 2, &combined, &loop), UVRPC_OK);

    uvrpc_promise_resolve(a, (const uint8_t*)"first", 5);
    EXPECT_TRUE(PumpUntil(&loop, [&] { return uvrpc_promise_is_fulfilled(&combined); }));
    EXPECT_EQ(uvrpc_promise_is_fulfilled(&combined), 1);

    /* The loser settles afterwards. Every raced promise was handed the same
     * context pointer, so freeing it from the first callback left this one
     * reading freed memory -- a use-after-free the ASan job would have caught
     * if anything had exercised race() at all.
     *
     * The context is released once the last of them reports, so the loop has to
     * keep running after the loser settles. resolve() marks the promise
     * fulfilled synchronously but delivers the callback on a later iteration,
     * so "is it fulfilled" is already true and cannot be the thing waited on
     * here -- pump a fixed number of iterations instead, and let the leak
     * checker confirm the context was actually freed. */
    uvrpc_promise_resolve(b, (const uint8_t*)"second", 6);
    for (int i = 0; i < 10; i++) {
        if (uv_run(&loop, UV_RUN_ONCE) == 0) break;
    }
    EXPECT_EQ(uvrpc_promise_is_fulfilled(b), 1);

    /* The combined promise keeps the winner's bytes. */
    uint8_t* result = nullptr;
    size_t size = 0;
    ASSERT_EQ(uvrpc_promise_get_result(&combined, &result, &size), UVRPC_OK);
    EXPECT_EQ(size, 5u);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(memcmp(result, "first", 5), 0);

    FinishCombined();
    uvrpc_promise_destroy(a);
    uvrpc_promise_destroy(b);
}

TEST_F(Promises, AllCombinesEveryResult) {
    auto* a = uvrpc_promise_create(&loop);
    auto* b = uvrpc_promise_create(&loop);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    InitCombined();

    uvrpc_promise_t* promises[] = {a, b};
    ASSERT_EQ(uvrpc_promise_all(promises, 2, &combined, &loop), UVRPC_OK);

    uvrpc_promise_resolve(a, (const uint8_t*)"aa", 2);
    uvrpc_promise_resolve(b, (const uint8_t*)"bbb", 3);

    EXPECT_EQ(uvrpc_promise_wait(&combined), UVRPC_OK);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(&combined), 1);

    uint8_t* result = nullptr;
    size_t size = 0;
    ASSERT_EQ(uvrpc_promise_get_result(&combined, &result, &size), UVRPC_OK);
    ASSERT_NE(result, nullptr);

    /* Layout, as uvrpc_promise_all() writes it: an int count, then per result
     * a size_t followed immediately by that result's bytes. Sizes are not
     * collected into a table. Results appear in completion order. */
    ASSERT_GE(size, sizeof(int));
    int count = 0;
    memcpy(&count, result, sizeof(count));
    EXPECT_EQ(count, 2);

    const uint8_t* cursor = result + sizeof(int);
    const uint8_t* const end = result + size;
    std::string joined;
    for (int i = 0; i < count && cursor + sizeof(size_t) <= end; i++) {
        size_t chunk = 0;
        memcpy(&chunk, cursor, sizeof(chunk));
        cursor += sizeof(chunk);
        ASSERT_LE(cursor + chunk, end);
        joined.append(reinterpret_cast<const char*>(cursor), chunk);
        cursor += chunk;
    }
    EXPECT_EQ(cursor, end) << "trailing bytes in the combined result";
    EXPECT_EQ(joined, "aabbb");

    FinishCombined();
    uvrpc_promise_destroy(a);
    uvrpc_promise_destroy(b);
}

TEST_F(Promises, AllRejectsWhenAnyPromiseRejects) {
    auto* a = uvrpc_promise_create(&loop);
    auto* b = uvrpc_promise_create(&loop);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    InitCombined();

    uvrpc_promise_t* promises[] = {a, b};
    ASSERT_EQ(uvrpc_promise_all(promises, 2, &combined, &loop), UVRPC_OK);

    uvrpc_promise_reject(a, UVRPC_ERROR_NOT_FOUND, "missing");
    /* The other one still has to report; the context has to outlive it. */
    uvrpc_promise_resolve(b, (const uint8_t*)"bb", 2);

    EXPECT_EQ(uvrpc_promise_wait(&combined), UVRPC_ERROR_NOT_FOUND);
    EXPECT_EQ(uvrpc_promise_is_rejected(&combined), 1);
    EXPECT_STREQ(uvrpc_promise_get_error(&combined), "missing");

    FinishCombined();
    uvrpc_promise_destroy(a);
    uvrpc_promise_destroy(b);
}

TEST_F(Promises, AllSettledCompletesEvenWhenOneRejects) {
    auto* a = uvrpc_promise_create(&loop);
    auto* b = uvrpc_promise_create(&loop);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    InitCombined();

    uvrpc_promise_t* promises[] = {a, b};
    ASSERT_EQ(uvrpc_promise_all_settled(promises, 2, &combined, &loop),
              UVRPC_OK);

    uvrpc_promise_resolve(a, (const uint8_t*)"ok", 2);
    uvrpc_promise_reject(b, UVRPC_ERROR_TIMEOUT, "too slow");

    EXPECT_EQ(uvrpc_promise_wait(&combined), UVRPC_OK);
    EXPECT_EQ(uvrpc_promise_is_fulfilled(&combined), 1);

    uint8_t* result = nullptr;
    size_t size = 0;
    ASSERT_EQ(uvrpc_promise_get_result(&combined, &result, &size), UVRPC_OK);
    ASSERT_NE(result, nullptr);

    /* Layout: an int count, then per promise a status byte (1 fulfilled,
     * 2 rejected) followed by that promise's payload -- the bytes for a
     * fulfilled one, the int32 code and the NUL-terminated message for a
     * rejected one. */
    ASSERT_GE(size, sizeof(int) + 2);
    int count = 0;
    memcpy(&count, result, sizeof(count));
    EXPECT_EQ(count, 2);

    const uint8_t* cursor = result + sizeof(int);
    const uint8_t* const end = result + size;
    int fulfilled = 0;
    int rejected = 0;
    std::string reason;
    for (int i = 0; i < count && cursor < end; i++) {
        const uint8_t status = *cursor++;
        if (status == 1) {
            fulfilled++;
            ASSERT_LE(cursor + sizeof(size_t), end);
            size_t chunk = 0;
            memcpy(&chunk, cursor, sizeof(chunk));
            cursor += sizeof(chunk);
            ASSERT_LE(cursor + chunk, end);
            cursor += chunk;
        } else if (status == 2) {
            rejected++;
            ASSERT_LE(cursor + sizeof(int32_t), end);
            int32_t code = 0;
            memcpy(&code, cursor, sizeof(code));
            EXPECT_EQ(code, UVRPC_ERROR_TIMEOUT);
            cursor += sizeof(code);
            const char* message = reinterpret_cast<const char*>(cursor);
            size_t remaining = static_cast<size_t>(end - cursor);
            ASSERT_LT(strlen(message), remaining);
            reason = message;
            cursor += strlen(message) + 1;
        } else {
            ADD_FAILURE() << "unexpected status " << static_cast<int>(status);
            break;
        }
    }
    EXPECT_EQ(fulfilled, 1);
    EXPECT_EQ(rejected, 1);
    EXPECT_EQ(reason, "too slow");

    FinishCombined();
    uvrpc_promise_destroy(a);
    uvrpc_promise_destroy(b);
}

TEST_F(Promises, CombinatorsRejectBadArguments) {
    InitCombined();
    uvrpc_promise_t* promises[] = {nullptr};

    EXPECT_EQ(uvrpc_promise_all(nullptr, 1, &combined, &loop),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_all(promises, 0, &combined, &loop),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_all(promises, 1, nullptr, &loop),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_all(promises, 1, &combined, nullptr),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_race(nullptr, 1, &combined, &loop),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_promise_all_settled(promises, 1, &combined, nullptr),
              UVRPC_ERROR_INVALID_PARAM);

    FinishCombined();
}

/* ------------------------------------------------------------ semaphore */

TEST_F(Promises, SemaphoreHandsOutPermitsAndGivesThemBack) {
    InitSemaphore(1);
    EXPECT_EQ(uvrpc_semaphore_get_available(&semaphore), 1);

    auto* first = uvrpc_promise_create(&loop);
    auto* second = uvrpc_promise_create(&loop);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    /* First takes the only permit. */
    EXPECT_EQ(uvrpc_semaphore_acquire_async(&semaphore, first), UVRPC_OK);
    EXPECT_EQ(uvrpc_semaphore_get_available(&semaphore), 0);
    EXPECT_TRUE(PumpUntil(&loop, [&] { return uvrpc_promise_is_fulfilled(first); }));

    /* Second has to queue rather than resolve. */
    EXPECT_EQ(uvrpc_semaphore_acquire_async(&semaphore, second), UVRPC_OK);
    EXPECT_EQ(uvrpc_semaphore_get_waiting_count(&semaphore), 1);
    EXPECT_EQ(uvrpc_promise_is_pending(second), 1);

    /* Releasing hands the permit to the waiter, not back to the pool. */
    EXPECT_EQ(uvrpc_semaphore_release(&semaphore), UVRPC_OK);
    EXPECT_TRUE(PumpUntil(&loop, [&] { return uvrpc_promise_is_fulfilled(second); }));
    EXPECT_EQ(uvrpc_semaphore_get_waiting_count(&semaphore), 0);
    EXPECT_EQ(uvrpc_semaphore_get_available(&semaphore), 0);

    /* Now the permit is genuinely free. */
    EXPECT_EQ(uvrpc_semaphore_release(&semaphore), UVRPC_OK);
    EXPECT_EQ(uvrpc_semaphore_get_available(&semaphore), 1);

    uvrpc_promise_destroy(first);
    uvrpc_promise_destroy(second);
    uvrpc_semaphore_cleanup(&semaphore);
    semaphore_armed = false;
}

TEST_F(Promises, SemaphoreTryAcquireNeverBlocks) {
    InitSemaphore(2);

    EXPECT_EQ(uvrpc_semaphore_try_acquire(&semaphore), 1);
    EXPECT_EQ(uvrpc_semaphore_try_acquire(&semaphore), 1);
    EXPECT_EQ(uvrpc_semaphore_try_acquire(&semaphore), 0);
    EXPECT_EQ(uvrpc_semaphore_try_acquire(nullptr), 0);
    EXPECT_EQ(uvrpc_semaphore_get_available(nullptr), 0);
    EXPECT_EQ(uvrpc_semaphore_get_waiting_count(nullptr), 0);
    EXPECT_EQ(uvrpc_semaphore_release(nullptr), UVRPC_ERROR_INVALID_PARAM);

    uvrpc_semaphore_cleanup(&semaphore);
    semaphore_armed = false;
}

TEST_F(Promises, SemaphoreRejectsBadArguments) {
    auto* promise = uvrpc_promise_create(&loop);

    EXPECT_EQ(uvrpc_semaphore_init(nullptr, &loop, 1), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_semaphore_init(&semaphore, nullptr, 1),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_semaphore_init(&semaphore, &loop, -1),
              UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_semaphore_acquire_async(nullptr, promise),
              UVRPC_ERROR_INVALID_PARAM);

    InitSemaphore(1);
    EXPECT_EQ(uvrpc_semaphore_acquire_async(&semaphore, nullptr),
              UVRPC_ERROR_INVALID_PARAM);

    uvrpc_semaphore_cleanup(nullptr);
    uvrpc_promise_destroy(promise);
}

/* ------------------------------------------------------------- waitgroup */

TEST_F(Promises, WaitGroupCountsUpAndDown) {
    InitWaitGroup();
    EXPECT_EQ(uvrpc_get_count(&waitgroup), 0);

    EXPECT_EQ(uvrpc_waitgroup_add(&waitgroup, 3), UVRPC_OK);
    EXPECT_EQ(uvrpc_get_count(&waitgroup), 3);
    EXPECT_EQ(uvrpc_waitgroup_done(&waitgroup), UVRPC_OK);
    EXPECT_EQ(uvrpc_get_count(&waitgroup), 2);

    /* A negative delta is how you subtract a batch. */
    EXPECT_EQ(uvrpc_waitgroup_add(&waitgroup, -2), UVRPC_OK);
    EXPECT_EQ(uvrpc_get_count(&waitgroup), 0);

    EXPECT_EQ(uvrpc_waitgroup_add(nullptr, 1), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_waitgroup_done(nullptr), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_get_count(nullptr), 0);
    EXPECT_EQ(uvrpc_waitgroup_init(nullptr, &loop), UVRPC_ERROR_INVALID_PARAM);
    EXPECT_EQ(uvrpc_waitgroup_init(&waitgroup, nullptr),
              UVRPC_ERROR_INVALID_PARAM);

    uvrpc_waitgroup_cleanup(nullptr);
}