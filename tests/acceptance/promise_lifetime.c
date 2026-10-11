/*
 * A promise owns a libuv handle, and a handle can only be given back with
 * uv_close() -- which does not close it. uv_close() *queues* the close; libuv
 * keeps pointing at the handle and reads it the next time the loop runs. Every
 * promise bug below is a variation on forgetting that.
 *
 * The three, with what they looked like when measured:
 *
 *  1. uvrpc_promise_destroy() released the struct on the spot, right after
 *     uv_close() was queued. valgrind, on a settled promise whose loop then
 *     turned over:
 *
 *         Invalid read of size 8 at uv_run
 *           144 bytes inside a block of size 200 free'd by
 *           uvrpc_promise_destroy, alloc'd at uvrpc_promise_create
 *
 *  2. uvrpc_promise_all_sync() and uvrpc_promise_race_sync() kept their
 *     combined promise in a local and cleaned it up on the way out, so the
 *     frame was gone before the queued close ran. Not latent at all -- with
 *     two already-resolved promises and an otherwise successful call:
 *
 *         all_sync -> UVRPC_OK
 *         uv_run()  -> SIGSEGV
 *
 *  3. uvrpc_promise_race() freed its context only once every participant had
 *     reported in. A race is decided by the first, and the ones that lose have
 *     no reason to finish:
 *
 *         24 bytes definitely lost, alloc'd by uvrpc_promise_race
 *
 * Why this is a C program under valgrind rather than another gtest case: only
 * (2) is visible to a plain run, and gtest's own stringstream teardown is
 * reported by this valgrind as 73 mismatched free() errors, which buries the
 * handful of real ones. This file allocates nothing through libstdc++, so
 * valgrind's output here is entirely the library's.
 *
 * Run: ctest -R promise_lifetime_memcheck
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#include "uvrpc.h"
#include "uvrpc_allocator.h"
#include "uvrpc_primitives.h"

static int g_failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

/* Walk the blob uvrpc_promise_all() hands back: an int count, then per result
 * a size_t followed immediately by that result's bytes. Returns the joined
 * bytes into `out` (which the caller provides), and the count, or -1 if the
 * framing does not parse. */
static int unpack_combined(const uint8_t* blob, size_t size, int expected_count,
                           char* out, size_t out_cap) {
    if (!blob || size < sizeof(int)) return -1;

    int count = 0;
    memcpy(&count, blob, sizeof(count));
    if (count != expected_count) return -1;

    const uint8_t* cursor = blob + sizeof(int);
    const uint8_t* const end = blob + size;
    size_t written = 0;

    for (int i = 0; i < count; i++) {
        if (cursor + sizeof(size_t) > end) return -1;
        size_t chunk = 0;
        memcpy(&chunk, cursor, sizeof(chunk));
        cursor += sizeof(chunk);
        if (cursor + chunk > end) return -1;
        if (written + chunk + 1 > out_cap) return -1;
        memcpy(out + written, cursor, chunk);
        written += chunk;
        cursor += chunk;
    }
    if (cursor != end) return -1;  /* trailing bytes: the framing did not parse */
    out[written] = '\0';
    return count;
}

static void pump(uv_loop_t* loop, int turns) {
    for (int i = 0; i < turns; i++) {
        if (uv_run(loop, UV_RUN_ONCE) == 0) break;
    }
}

/* (1) create/destroy churn. The struct is released by the queued close, so
 * this is only clean if nothing reads it after destroy() returns. */
static void test_destroy_churn(uv_loop_t* loop) {
    printf("[ RUN      ] destroy_churn\n");

    for (int i = 0; i < 64; i++) {
        uvrpc_promise_t* p = uvrpc_promise_create(loop);
        CHECK(p != NULL, "promise_create returned NULL on round %d", i);
        if (!p) continue;

        /* Alternate a settled promise with one left pending, because the two
         * paths take different amounts of state through destroy(). */
        if (i % 2 == 0) {
            uvrpc_promise_resolve(p, (const uint8_t*)"payload", 7);
        }
        uvrpc_promise_destroy(p);

        /* This is the read valgrind flags. It never crashes: the freed block
         * is normally still mapped and unclaimed. */
        pump(loop, 4);
    }

    pump(loop, 10);
    CHECK(uv_loop_close(loop) == 0, "loop did not close after destroy churn");
    CHECK(uv_loop_init(loop) == 0, "loop did not re-init");
    printf("[       OK ] destroy_churn\n");
}

/* (2) the blocking combinators, which put their combined promise on the stack
 * and cleaned it up on the way out. */
static void test_sync_combinators(uv_loop_t* loop) {
    printf("[ RUN      ] sync_combinators\n");

    uvrpc_promise_t* a = uvrpc_promise_create(loop);
    uvrpc_promise_t* b = uvrpc_promise_create(loop);
    CHECK(a && b, "promise_create failed");
    if (a && b) {
        uvrpc_promise_resolve(a, (const uint8_t*)"aa", 2);
        uvrpc_promise_resolve(b, (const uint8_t*)"bbb", 3);

        uvrpc_promise_t* pair[2] = {a, b};
        uint8_t* result = NULL;
        size_t size = 0;

        int rc = uvrpc_promise_all_sync(pair, 2, &result, &size, loop);
        CHECK(rc == UVRPC_OK, "all_sync returned %d, want UVRPC_OK", rc);

        /* The combined promise has been released by the time this runs, and
         * the loop still has its close to execute. That is the SIGSEGV. */
        pump(loop, 10);

        char joined[64];
        memset(joined, 0, sizeof(joined));
        if (rc == UVRPC_OK && result) {
            int count = unpack_combined(result, size, 2, joined, sizeof(joined));
            CHECK(count == 2, "combined framing gave count %d, want 2", count);
            CHECK(strcmp(joined, "aabbb") == 0,
                  "combined payload \"%s\", want \"aabbb\"", joined);
            uvrpc_free(result);
        } else {
            CHECK(0, "all_sync returned no result");
        }
    }

    /* race_sync: one already settled, one that reports in late. The late one
     * matters -- it is the loser whose callback used to outlive the context. */
    uvrpc_promise_t* fast = uvrpc_promise_create(loop);
    uvrpc_promise_t* slow = uvrpc_promise_create(loop);
    CHECK(fast && slow, "promise_create failed");
    if (fast && slow) {
        uvrpc_promise_resolve(fast, (const uint8_t*)"winner", 6);

        uvrpc_promise_t* pair[2] = {slow, fast};
        uint8_t* result = NULL;
        size_t size = 0;

        int rc = uvrpc_promise_race_sync(pair, 2, &result, &size, loop);
        CHECK(rc == UVRPC_OK, "race_sync returned %d, want UVRPC_OK", rc);
        pump(loop, 10);

        CHECK(rc == UVRPC_OK && result != NULL, "race_sync returned no result");
        if (rc == UVRPC_OK && result) {
            CHECK(size == 6 && memcmp(result, "winner", 6) == 0,
                  "race_sync payload is %.*s, want \"winner\"", (int)size,
                  (const char*)result);
            uvrpc_free(result);
        }

        /* Loser reports in after race_sync has already released the combined
         * promise. Its callback must not go near that memory. */
        uvrpc_promise_resolve(slow, (const uint8_t*)"late", 4);
        pump(loop, 10);
    }

    uvrpc_promise_destroy(a);
    uvrpc_promise_destroy(b);
    uvrpc_promise_destroy(fast);
    uvrpc_promise_destroy(slow);
    pump(loop, 10);

    printf("[       OK ] sync_combinators\n");
}

/* (3) a race with a participant that never finishes. valgrind's leak check is
 * the only thing that sees this -- nothing crashes and 24 bytes is nothing. */
static void test_race_with_a_never_finishing_loser(uv_loop_t* loop) {
    printf("[ RUN      ] race_with_a_never_finishing_loser\n");

    uvrpc_promise_t* winner = uvrpc_promise_create(loop);
    uvrpc_promise_t* loser = uvrpc_promise_create(loop);
    uvrpc_promise_t* combined = uvrpc_promise_create(loop);
    CHECK(winner && loser && combined, "promise_create failed");

    if (winner && loser && combined) {
        uvrpc_promise_t* pair[2] = {winner, loser};

        int rc = uvrpc_promise_race(pair, 2, combined, loop);
        CHECK(rc == UVRPC_OK, "race returned %d, want UVRPC_OK", rc);

        uvrpc_promise_resolve(winner, (const uint8_t*)"first", 5);
        CHECK(uvrpc_promise_wait(combined) == UVRPC_OK,
              "wait on the combined promise did not succeed");

        uint8_t* out = NULL;
        size_t out_size = 0;
        CHECK(uvrpc_promise_get_result(combined, &out, &out_size) == UVRPC_OK,
              "no result from the combined promise");
        CHECK(out_size == 5 && out && memcmp(out, "first", 5) == 0,
              "combined payload wrong");

        /* Well past the point where anything could fire, and `loser` is never
         * going to settle. The context is released when the race is decided,
         * not when the field is empty. */
        pump(loop, 25);
    }

    uvrpc_promise_destroy(winner);
    uvrpc_promise_destroy(loser);
    uvrpc_promise_destroy(combined);
    pump(loop, 10);

    printf("[       OK ] race_with_a_never_finishing_loser\n");
}

int main(void) {
    uv_loop_t loop;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (uv_loop_init(&loop) != 0) {
        printf("FAIL: uv_loop_init\n");
        return 1;
    }

    test_destroy_churn(&loop);
    test_sync_combinators(&loop);
    test_race_with_a_never_finishing_loser(&loop);

    /* Close once more so the handles these tests opened are all accounted for
     * before valgrind takes its snapshot. */
    pump(&loop, 20);
    int rc = uv_loop_close(&loop);
    CHECK(rc == 0, "uv_loop_close returned %d, want 0 (EBUSY=%d) -- a handle "
                   "was never closed", rc, UV_EBUSY);

    if (g_failures) {
        printf("\npromise_lifetime: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("\npromise_lifetime: all checks passed\n");
    return 0;
}
