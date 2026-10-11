/**
 * @file uvrpc_primitives.c
 * @brief UVRPC Async Programming Primitives Implementation
 * 
 * Implementation of Promise/Future, Semaphore, and Barrier patterns.
 * All primitives are designed to work with libuv event loop without blocking.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc_primitives.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Use uvrpc_alloc/uvrpc_free for memory allocation */
#define UVRPC_MALLOC(size) uvrpc_alloc(size)
#define UVRPC_FREE(ptr) uvrpc_free(ptr)

/* ============================================================================
 * Promise/Future Implementation
 * ============================================================================ */

/* Async callback for promise completion */
static void promise_async_callback(uv_async_t* handle) {
    uvrpc_promise_t* promise = (uvrpc_promise_t*)handle->data;

    if (promise->callback && !promise->is_callback_scheduled) {
        promise->is_callback_scheduled = 1;
        promise->callback(promise, promise->callback_data);
        promise->is_callback_scheduled = 0;
        /* Unref after callback fires so uv_run can return */
        uv_unref((uv_handle_t*)handle);
    }
}

/* Initialize promise */
int uvrpc_promise_init(uvrpc_promise_t* promise, uv_loop_t* loop) {
    if (!promise || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    memset(promise, 0, sizeof(uvrpc_promise_t));
    promise->loop = loop;
    promise->state = UVRPC_PROMISE_PENDING;
    promise->result = NULL;
    promise->result_size = 0;
    promise->error_message = NULL;
    promise->error_code = 0;
    promise->callback = NULL;
    promise->callback_data = NULL;
    promise->is_callback_scheduled = 0;
    
    int ret = uv_async_init(loop, &promise->async_handle, promise_async_callback);
    if (ret != 0) {
        return UVRPC_ERROR;
    }
    promise->async_handle.data = promise;
    uv_unref((uv_handle_t*)&promise->async_handle); /* Don't prevent loop exit */
    
    return UVRPC_OK;
}

/* Release the promise's payload -- result bytes, error message, and the state
 * that describes them. Says nothing about the async handle, so it is safe to
 * call from a path where the handle's close is already queued. */
static void promise_release_payload(uvrpc_promise_t* promise) {
    if (promise->result) {
        uvrpc_free(promise->result);
        promise->result = NULL;
    }

    if (promise->error_message) {
        uvrpc_free(promise->error_message);
        promise->error_message = NULL;
    }

    promise->state = UVRPC_PROMISE_PENDING;
    promise->result_size = 0;
    promise->error_code = 0;
    promise->callback = NULL;
    promise->callback_data = NULL;
}

/* Close callback for a promise uvrpc_promise_destroy() is releasing.
 *
 * uv_close() only *queues* the close: libuv keeps a pointer to the handle and
 * reads it when the loop next runs, so the struct cannot go back to the
 * allocator before that. uvrpc_promise_destroy() used to call cleanup() and
 * then free immediately, which handed libuv memory that was already gone.
 * Measured with valgrind, on a promise that was settled before being
 * destroyed and whose loop then turned over:
 *
 *     Invalid read of size 8 at uv_run
 *       144 bytes inside a block of size 200 free'd by uvrpc_promise_destroy
 *       block alloc'd at uvrpc_promise_create
 *
 * It does not crash in practice only because the freed block is usually still
 * mapped and unclaimed -- which is the worst kind of not-crashing. Where the
 * struct is a stack local it is worse than latent: uvrpc_promise_all_sync()
 * and uvrpc_promise_race_sync() put theirs on the stack, and after they
 * returned the next uv_run() segfaulted.
 *
 * async_handle.data is the promise, set by uvrpc_promise_init() and owned by
 * the library. This callback is only ever attached by destroy(), so reaching
 * it always means "release the struct". */
static void promise_handle_closed(uv_handle_t* handle) {
    uvrpc_promise_t* promise = (uvrpc_promise_t*)handle->data;
    handle->data = NULL;
    uvrpc_free(promise);
}

/* Cleanup promise.
 *
 * Releases the payload and closes the async handle, but does NOT free the
 * promise struct: whether it is a stack local, a caller's own allocation, or a
 * uvrpc_promise_create() result is not this function's business. The caller
 * keeps ownership and has to keep the struct valid until the loop has run the
 * close -- uv_close() does not take effect synchronously. That constraint is
 * what uvrpc_promise_all_sync() got wrong; it is documented on the declaration
 * in the header. */
void uvrpc_promise_cleanup(uvrpc_promise_t* promise) {
    if (!promise) return;

    promise_release_payload(promise);

    /* Close async handle if not closing */
    if (!uv_is_closing((uv_handle_t*)&promise->async_handle)) {
        uv_close((uv_handle_t*)&promise->async_handle, NULL);
    }
}

/* Create and initialize a Promise (convenience function) */
uvrpc_promise_t* uvrpc_promise_create(uv_loop_t* loop) {
    if (!loop) {
        return NULL;
    }
    
    uvrpc_promise_t* promise = (uvrpc_promise_t*)UVRPC_MALLOC(sizeof(uvrpc_promise_t));
    if (!promise) {
        return NULL;
    }
    
    int ret = uvrpc_promise_init(promise, loop);
    if (ret != UVRPC_OK) {
        UVRPC_FREE(promise);
        return NULL;
    }
    
    return promise;
}

/* Cleanup and free a Promise (convenience function).
 *
 * The struct is released by the close callback rather than on the spot:
 * uv_close() only queues the close, so libuv still points at the handle and
 * reads it on the next loop turn. Freeing here first was a use-after-free on
 * every promise this function ever released -- see promise_handle_closed()
 * for the valgrind report. The consequence for callers is that the memory
 * lives one loop turn longer than destroy() returns; nothing is reachable
 * after this point, since the payload was released above.
 *
 * uv_close() must not be queued twice -- libuv asserts on it -- so a promise
 * already passed to uvrpc_promise_cleanup() has a close on the loop with no
 * callback attached, and this cannot safely take it over or free the struct
 * under it. That sequence (cleanup, then destroy) leaks deliberately rather
 * than freeing memory the queued close still refers to, and no code in this
 * tree does it. */
void uvrpc_promise_destroy(uvrpc_promise_t* promise) {
    if (!promise) {
        return;
    }

    promise_release_payload(promise);

    if (uv_is_closing((uv_handle_t*)&promise->async_handle)) {
        return;
    }

    uv_close((uv_handle_t*)&promise->async_handle, promise_handle_closed);
}

/* Wait for Promise to complete synchronously (blocking) */
int uvrpc_promise_wait(uvrpc_promise_t* promise) {
    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Run the event loop until the promise is settled.
     *
     * UV_RUN_ONCE, not UV_RUN_DEFAULT: DEFAULT only returns once the loop has
     * no referenced handles left, and a single connected client is enough to
     * keep the loop referenced forever. Under DEFAULT the response that settles
     * the promise is processed *inside* the call that never returns, so the
     * answer arrives and wait() still never comes back. ONCE returns once a
     * batch of events has been handled, which is what lets this loop re-check
     * the state.
     *
     * ran == 0 means the loop has nothing left to do, so the event that would
     * settle this promise cannot fire. Continuing would spin. */
    while (promise->state == UVRPC_PROMISE_PENDING) {
        int ran = uv_run(promise->loop, UV_RUN_ONCE);
        if (ran == 0) {
            break;
        }
    }

    if (promise->state == UVRPC_PROMISE_FULFILLED) {
        return UVRPC_OK;
    }
    if (promise->state == UVRPC_PROMISE_REJECTED) {
        return promise->error_code;
    }
    /* Still pending. Returning error_code here would return 0 -- UVRPC_OK --
     * because a pending promise has no error code, and the caller would go on
     * to read a result that does not exist. Say we gave up instead. */
    return UVRPC_ERROR_TIMEOUT;
}

/* Resolve promise */
int uvrpc_promise_resolve(uvrpc_promise_t* promise, const uint8_t* result, size_t result_size) {
    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (promise->state != UVRPC_PROMISE_PENDING) {
        return UVRPC_ERROR_INVALID_STATE;
    }
    
    /* Copy result */
    if (result && result_size > 0) {
        promise->result = uvrpc_alloc(result_size);
        if (!promise->result) {
            return UVRPC_ERROR_NO_MEMORY;
        }
        memcpy(promise->result, result, result_size);
        promise->result_size = result_size;
    }
    
    promise->state = UVRPC_PROMISE_FULFILLED;
    
    /* Schedule callback */
    if (promise->callback) {
        uv_ref((uv_handle_t*)&promise->async_handle); /* Keep loop alive */
        uv_async_send(&promise->async_handle);
    }
    
    return UVRPC_OK;
}

/* Reject promise */
int uvrpc_promise_reject(uvrpc_promise_t* promise, int32_t error_code, const char* error_message) {
    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (promise->state != UVRPC_PROMISE_PENDING) {
        return UVRPC_ERROR_INVALID_STATE;
    }
    
    /* Copy error message */
    if (error_message) {
        promise->error_message = uvrpc_strdup(error_message);
        if (!promise->error_message) {
            return UVRPC_ERROR_NO_MEMORY;
        }
    }
    
    promise->error_code = error_code;
    promise->state = UVRPC_PROMISE_REJECTED;
    
    /* Schedule callback */
    if (promise->callback) {
        uv_ref((uv_handle_t*)&promise->async_handle); /* Keep loop alive */
        uv_async_send(&promise->async_handle);
    }
    
    return UVRPC_OK;
}

/* Set completion callback */
int uvrpc_promise_then(uvrpc_promise_t* promise, uvrpc_promise_callback_t callback, void* user_data) {
    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    promise->callback = callback;
    promise->callback_data = user_data;
    
    /* If already completed, schedule callback */
    if (promise->state != UVRPC_PROMISE_PENDING && callback) {
        uv_ref((uv_handle_t*)&promise->async_handle);
        uv_async_send(&promise->async_handle);
    }
    
    return UVRPC_OK;
}

/* Set intermediate callback for chaining (used internally) */
int uvrpc_promise_set_callback(uvrpc_promise_t* promise, uvrpc_promise_callback_t callback, void* user_data) {
    return uvrpc_promise_then(promise, callback, user_data);
}

/* Check state */
int uvrpc_promise_is_fulfilled(uvrpc_promise_t* promise) {
    return promise ? (promise->state == UVRPC_PROMISE_FULFILLED) : 0;
}

int uvrpc_promise_is_rejected(uvrpc_promise_t* promise) {
    return promise ? (promise->state == UVRPC_PROMISE_REJECTED) : 0;
}

int uvrpc_promise_is_pending(uvrpc_promise_t* promise) {
    return promise ? (promise->state == UVRPC_PROMISE_PENDING) : 0;
}

/* Get result */
int uvrpc_promise_get_result(uvrpc_promise_t* promise, uint8_t** result, size_t* result_size) {
    if (!result || !result_size) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* Clear the outputs before anything can fail. On the failure paths below
     * they would otherwise be left exactly as the caller passed them, and the
     * documented usage ignores this function's return value:
     *     uvrpc_promise_get_result(p, &result, &size);  // use result
     * so an unwritten *size hands back whatever was on the stack. */
    *result = NULL;
    *result_size = 0;

    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (promise->state != UVRPC_PROMISE_FULFILLED) {
        return UVRPC_ERROR_INVALID_STATE;
    }

    *result = promise->result;
    *result_size = promise->result_size;

    return UVRPC_OK;
}

/* Get error */
const char* uvrpc_promise_get_error(uvrpc_promise_t* promise) {
    return promise ? promise->error_message : NULL;
}

int32_t uvrpc_promise_get_error_code(uvrpc_promise_t* promise) {
    return promise ? promise->error_code : 0;
}

/* ============================================================================
 * Semaphore Implementation
 * ============================================================================ */

/* Semaphore waiting entry */
struct semaphore_waiter {
    uvrpc_promise_t* promise;
    struct semaphore_waiter* next;
};

/* Async callback for semaphore - processes per-semaphore waiter queue */
static void semaphore_async_callback(uv_async_t* handle) {
    uvrpc_semaphore_t* semaphore = (uvrpc_semaphore_t*)handle->data;

    while (semaphore->waiter_head != NULL && semaphore->permits > 0) {
        semaphore_waiter_t* waiter = semaphore->waiter_head;
        semaphore->waiter_head = waiter->next;

        /* Acquire permit */
        semaphore->permits--;
        semaphore->waiting--;

        /* Resolve the Promise */
        if (waiter->promise) {
            int result = 1;
            uvrpc_promise_resolve(waiter->promise, (uint8_t*)&result, sizeof(int));
        }

        uvrpc_free(waiter);
    }
}

/* Initialize semaphore */
int uvrpc_semaphore_init(uvrpc_semaphore_t* semaphore, uv_loop_t* loop, int permits) {
    if (!semaphore || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (permits < 0) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    memset(semaphore, 0, sizeof(uvrpc_semaphore_t));
    semaphore->loop = loop;
    semaphore->permits = permits;
    semaphore->waiting = 0;
    semaphore->waiter_head = NULL;

    int ret = uv_async_init(loop, &semaphore->async_handle, semaphore_async_callback);
    if (ret != 0) {
        return UVRPC_ERROR;
    }
    semaphore->async_handle.data = semaphore;
    uv_unref((uv_handle_t*)&semaphore->async_handle);

    return UVRPC_OK;
}

/* Cleanup semaphore */
void uvrpc_semaphore_cleanup(uvrpc_semaphore_t* semaphore) {
    if (!semaphore) return;

    /* Free remaining waiters */
    while (semaphore->waiter_head) {
        semaphore_waiter_t* waiter = semaphore->waiter_head;
        semaphore->waiter_head = waiter->next;
        uvrpc_free(waiter);
    }

    if (!uv_is_closing((uv_handle_t*)&semaphore->async_handle)) {
        uv_close((uv_handle_t*)&semaphore->async_handle, NULL);
    }

    semaphore->permits = 0;
    semaphore->waiting = 0;
}

/* Release semaphore */
int uvrpc_semaphore_release(uvrpc_semaphore_t* semaphore) {
    if (!semaphore) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    int has_waiters = (semaphore->waiter_head != NULL);

    /* Increment permit count */
    semaphore->permits++;

    /* If there are waiters, process them */
    if (has_waiters) {
        uv_ref((uv_handle_t*)&semaphore->async_handle);
        uv_async_send(&semaphore->async_handle);
    }

    return UVRPC_OK;
}

/* Try acquire (immediate) */
int uvrpc_semaphore_try_acquire(uvrpc_semaphore_t* semaphore) {
    if (!semaphore) {
        return 0;
    }

    if (semaphore->permits > 0) {
        semaphore->permits--;
        return 1;
    }

    return 0;
}

/* Get available permits */
int uvrpc_semaphore_get_available(uvrpc_semaphore_t* semaphore) {
    return semaphore ? semaphore->permits : 0;
}

/* Get waiting count */
int uvrpc_semaphore_get_waiting_count(uvrpc_semaphore_t* semaphore) {
    return semaphore ? semaphore->waiting : 0;
}

/* Acquire semaphore asynchronously (JavaScript-style) */
int uvrpc_semaphore_acquire_async(uvrpc_semaphore_t* semaphore,
                                    uvrpc_promise_t* promise) {
    if (!semaphore || !promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (semaphore->permits > 0) {
        /* Permit available immediately */
        semaphore->permits--;

        /* Resolve the promise */
        int result = 1;
        return uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
    }

    /* No permit available, queue the promise */
    semaphore_waiter_t* waiter = (semaphore_waiter_t*)UVRPC_MALLOC(sizeof(semaphore_waiter_t));
    if (!waiter) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    waiter->promise = promise;
    waiter->next = NULL;

    /* Append to per-semaphore waiter queue */
    if (semaphore->waiter_head == NULL) {
        semaphore->waiter_head = waiter;
    } else {
        semaphore_waiter_t* tail = semaphore->waiter_head;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = waiter;
    }

    semaphore->waiting++;

    return UVRPC_OK;
}

/* ============================================================================
 * WaitGroup Implementation
 * ============================================================================ */

/* Async callback for waitgroup */
static void waitgroup_async_callback(uv_async_t* handle) {
    uvrpc_waitgroup_t* wg = (uvrpc_waitgroup_t*)handle->data;
    
    /* Resolve completion promise if set */
    /* Note: In a real implementation, we would track the completion promise here */
    /* For now, this is a simplified version */
    
    /* Mark callback as not scheduled */
    wg->is_callback_scheduled = 0;
}

/* Initialize waitgroup */
int uvrpc_waitgroup_init(uvrpc_waitgroup_t* wg, uv_loop_t* loop) {
    if (!wg || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    memset(wg, 0, sizeof(uvrpc_waitgroup_t));
    wg->loop = loop;
    wg->count = 0;
    wg->is_callback_scheduled = 0;
    
    int ret = uv_async_init(loop, &wg->async_handle, waitgroup_async_callback);
    if (ret != 0) {
        return UVRPC_ERROR;
    }
    wg->async_handle.data = wg;
    uv_unref((uv_handle_t*)&wg->async_handle);
    
    return UVRPC_OK;
}

/* Cleanup waitgroup */
void uvrpc_waitgroup_cleanup(uvrpc_waitgroup_t* wg) {
    if (!wg) return;
    
    /* Detach the completion promise. It belongs to the caller, and after this
     * the wait group is torn down -- so a done() that somehow still ran would
     * have nothing left to settle. */
    wg->completion_promise = NULL;

    if (!uv_is_closing((uv_handle_t*)&wg->async_handle)) {
        uv_close((uv_handle_t*)&wg->async_handle, NULL);
    }
    
    wg->count = 0;
}

/* Add to waitgroup */
int uvrpc_waitgroup_add(uvrpc_waitgroup_t* wg, int delta) {
    if (!wg) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    wg->count += delta;
    return UVRPC_OK;
}

/* Settle the wait group's completion promise, if one was taken and has not
 * settled yet. Declared here because done() reaches it and the definition
 * reads better next to get_promise(). */
static void waitgroup_settle(uvrpc_waitgroup_t* wg);

/* Signal one operation done */
int uvrpc_waitgroup_done(uvrpc_waitgroup_t* wg) {
    if (!wg) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Decrement the pending operation count */
    --wg->count;

    /* Count reached zero: settle the completion promise, if the caller asked
     * for one. Resolving is what makes the documented pattern work -- the
     * promise used to be settled by uvrpc_waitgroup_get_promise() itself, at
     * once, whatever the count was, so a callback fired while every operation
     * was still outstanding. */
    if (wg->count == 0) {
        waitgroup_settle(wg);
    }

    return UVRPC_OK;
}

/* Get count */
int uvrpc_get_count(uvrpc_waitgroup_t* wg) {
    return wg ? wg->count : 0;
}

/* Settle the completion promise, once, if there is one.
 *
 * The promise belongs to the caller -- uvrpc_waitgroup_init() did not allocate
 * it and uvrpc_waitgroup_cleanup() does not free it -- so this only ever
 * resolves, never releases. The state check is what makes it idempotent: done()
 * can reach zero more than once across add()/done() cycles, and a promise that
 * has already settled must not be settled again. */
static void waitgroup_settle(uvrpc_waitgroup_t* wg) {
    if (!wg->completion_promise) return;
    if (wg->completion_promise->state != UVRPC_PROMISE_PENDING) return;

    int result = 0;
    uvrpc_promise_resolve(wg->completion_promise, (uint8_t*)&result,
                          sizeof(int));
    /* Detached: a later cycle of add()/done() must not settle a promise the
     * caller has already been handed and may have cleaned up. */
    wg->completion_promise = NULL;
}

/* Get completion promise (JavaScript-style) */
int uvrpc_waitgroup_get_promise(uvrpc_waitgroup_t* wg, uvrpc_promise_t* promise) {
    if (!wg || !promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* An idle wait group is already satisfied, so this settles at once -- the
     * case the old unconditional resolve happened to get right. With work
     * outstanding the promise stays pending until the count reaches zero, which
     * is what uvrpc_waitgroup_done() now does. */
    wg->completion_promise = promise;
    if (wg->count <= 0) {
        waitgroup_settle(wg);
    }

    return UVRPC_OK;
}

/* ============================================================================
 * Promise Combinators (JavaScript-style)
 * ============================================================================ */

/* Context for Promise.all() */
typedef struct {
    uvrpc_promise_t* combined;
    int total_count;
    int completed_count;
    int rejected;
    /* How many promises have not reported yet. Every one of them holds this
     * pointer, so the context is freed only once the last one is done -- which
     * is also what makes the early return on rejection safe. */
    int remaining;
    uint8_t** results;
    size_t* result_sizes;
    uv_loop_t* loop;
} promise_all_context_t;

/* Context for Promise.race() */
typedef struct {
    uvrpc_promise_t* combined;
    int completed;
    uv_loop_t* loop;
    /* A copy of the caller's array, not a pointer to it: the caller's array
     * has no reason to outlive the call, and this context needs to reach every
     * participant after the race has been decided. See
     * on_promise_race_callback(). */
    uvrpc_promise_t** participants;
    int participant_count;
} promise_race_context_t;

/* Stands in for on_promise_race_callback() once the race is over.
 *
 * Not NULL, deliberately. promise_async_callback() only runs its uv_unref()
 * for a handle whose callback is set, and a participant is uv_ref()'d the
 * moment it resolves or rejects. Replacing the callback with NULL instead of
 * with this would leave the loser with a ref that nobody ever drops, and a
 * loop with a referenced handle and nothing pending blocks forever in
 * uv_run(UV_RUN_ONCE) -- the next wait would hang rather than fail. */
static void promise_race_noop(uvrpc_promise_t* promise, void* user_data) {
    (void)promise;
    (void)user_data;
}

/* Context for Promise.allSettled() */
typedef struct {
    uvrpc_promise_t* combined;
    int total_count;
    int completed_count;
    uint8_t** results;
    size_t* result_sizes;
    char** errors;
    int32_t* error_codes;
    uint8_t* statuses; /* 0=pending, 1=fulfilled, 2=rejected */
    uv_loop_t* loop;
} promise_all_settled_context_t;

/* Release a Promise.all() context and the result copies it holds. */
static void promise_all_context_free(promise_all_context_t* ctx) {
    for (int i = 0; i < ctx->total_count; i++) {
        if (ctx->results && ctx->results[i]) {
            UVRPC_FREE(ctx->results[i]);
        }
    }
    if (ctx->results) UVRPC_FREE(ctx->results);
    if (ctx->result_sizes) UVRPC_FREE(ctx->result_sizes);
    UVRPC_FREE(ctx);
}

/* Callback for Promise.all() individual promise */
static void on_promise_all_callback(uvrpc_promise_t* promise, void* user_data) {
    promise_all_context_t* ctx = (promise_all_context_t*)user_data;

    /* Once one promise has rejected the combined one is already rejected, but
     * this context still has to outlive the promises that have not reported
     * yet -- they all hold this pointer. So every path below ends by counting
     * one down, and the last one out frees. */
    if (ctx->rejected) {
        if (--ctx->remaining == 0) {
            promise_all_context_free(ctx);
        }
        return;
    }

    /* Check if this promise rejected */
    if (uvrpc_promise_is_rejected(promise)) {
        /* Mark as rejected and reject combined promise */
        ctx->rejected = 1;

        const char* error = uvrpc_promise_get_error(promise);
        int32_t error_code = uvrpc_promise_get_error_code(promise);
        uvrpc_promise_reject(ctx->combined, error_code, error);

        if (--ctx->remaining == 0) {
            promise_all_context_free(ctx);
        }
        return;
    }
    
    /* Get result from fulfilled promise */
    uint8_t* result = NULL;
    size_t result_size = 0;
    int ret = uvrpc_promise_get_result(promise, &result, &result_size);
    if (ret == UVRPC_OK) {
        /* Copy result */
        ctx->results[ctx->completed_count] = UVRPC_MALLOC(result_size);
        if (ctx->results[ctx->completed_count]) {
            memcpy(ctx->results[ctx->completed_count], result, result_size);
            ctx->result_sizes[ctx->completed_count] = result_size;
        }
    }
    
    /* Increment completed count */
    int completed = ++ctx->completed_count;
    --ctx->remaining;
    
    /* If all completed, resolve combined promise */
    if (completed == ctx->total_count) {
        /* Create combined result: array of all results */
        size_t total_size = sizeof(int) + ctx->total_count * sizeof(size_t);
        for (int i = 0; i < ctx->total_count; i++) {
            total_size += ctx->result_sizes[i];
        }
        
        uint8_t* combined_result = UVRPC_MALLOC(total_size);
        if (combined_result) {
            uint8_t* ptr = combined_result;
            
            /* Write count */
            memcpy(ptr, &ctx->total_count, sizeof(int));
            ptr += sizeof(int);
            
            /* Write result sizes and data */
            for (int i = 0; i < ctx->total_count; i++) {
                memcpy(ptr, &ctx->result_sizes[i], sizeof(size_t));
                ptr += sizeof(size_t);
                if (ctx->results[i]) {
                    memcpy(ptr, ctx->results[i], ctx->result_sizes[i]);
                    ptr += ctx->result_sizes[i];
                }
            }
            
            uvrpc_promise_resolve(ctx->combined, combined_result, total_size);
            UVRPC_FREE(combined_result);
        } else {
            uvrpc_promise_reject(ctx->combined, UVRPC_ERROR_NO_MEMORY, "Failed to allocate combined result");
        }
        
        /* Cleanup context */
        promise_all_context_free(ctx);
    }
}

/* Promise.all() - Wait for all promises to fulfill */
int uvrpc_promise_all(uvrpc_promise_t** promises, int count, uvrpc_promise_t* combined, uv_loop_t* loop) {
    if (!promises || count <= 0 || !combined || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Allocate context */
    promise_all_context_t* ctx = (promise_all_context_t*)UVRPC_MALLOC(sizeof(promise_all_context_t));
    if (!ctx) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    ctx->combined = combined;
    ctx->total_count = count;
    ctx->completed_count = 0;
    ctx->rejected = 0;
    ctx->remaining = count;
    ctx->loop = loop;
    ctx->results = NULL;
    ctx->result_sizes = NULL;
    
    /* Allocate result arrays */
    ctx->results = (uint8_t**)UVRPC_MALLOC(count * sizeof(uint8_t*));
    ctx->result_sizes = (size_t*)UVRPC_MALLOC(count * sizeof(size_t));
    if (!ctx->results || !ctx->result_sizes) {
        if (ctx->results) UVRPC_FREE(ctx->results);
        if (ctx->result_sizes) UVRPC_FREE(ctx->result_sizes);
        UVRPC_FREE(ctx);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memset(ctx->results, 0, count * sizeof(uint8_t*));
    memset(ctx->result_sizes, 0, count * sizeof(size_t));
    
    /* Set callbacks for all promises */
    for (int i = 0; i < count; i++) {
        uvrpc_promise_set_callback(promises[i], on_promise_all_callback, ctx);
    }
    
    return UVRPC_OK;
}

/* Callback for Promise.race() individual promise */
static void on_promise_race_callback(uvrpc_promise_t* promise, void* user_data) {
    promise_race_context_t* ctx = (promise_race_context_t*)user_data;

    /* The race is decided by the first callback to reach here, so this is
     * also where the context stops needing to exist -- see further down for
     * why it cannot just be left counting down. Safe because uv_async_send
     * queues callbacks for the next event loop iteration, so they cannot
     * preempt each other within the same tick. */
    if (!ctx->completed) {
        ctx->completed = 1;

        /* Forward result/rejection */
        if (uvrpc_promise_is_fulfilled(promise)) {
            uint8_t* result = NULL;
            size_t result_size = 0;
            int ret = uvrpc_promise_get_result(promise, &result, &result_size);
            if (ret == UVRPC_OK) {
                uint8_t* result_copy = UVRPC_MALLOC(result_size);
                if (result_copy) {
                    memcpy(result_copy, result, result_size);

                    uvrpc_promise_resolve(ctx->combined, result_copy, result_size);
                    UVRPC_FREE(result_copy);
                } else {
                    uvrpc_promise_reject(ctx->combined, UVRPC_ERROR_NO_MEMORY, "Failed to allocate result copy");
                }
            } else {
                uvrpc_promise_reject(ctx->combined, ret, "Failed to get result");
            }
        } else if (uvrpc_promise_is_rejected(promise)) {
            const char* error = uvrpc_promise_get_error(promise);
            int32_t error_code = uvrpc_promise_get_error_code(promise);
            uvrpc_promise_reject(ctx->combined, error_code, error);
        }

        /* Detach the rest and release the context now, instead of waiting for
         * every participant to report in.
         *
         * The old arrangement counted `remaining` down to 0 and freed on the
         * last report, because freeing here had already been tried and left
         * the losers reading a `completed` flag out of freed memory. But a
         * loser that never settles never reports, and a race is over the
         * moment it is decided -- there is nothing left to wait for. The two
         * together leaked this context on every race that had a participant
         * which did not finish: measured under valgrind, 24 bytes definitely
         * lost, allocated by uvrpc_promise_race(). Silent by nature, since
         * nothing crashes and the block is small.
         *
         * Detaching is what makes freeing here sound. Each participant is
         * swapped to promise_race_noop() *only* where it is still running this
         * race's callback -- same callback, same context -- so a callback the
         * caller installed afterwards is left untouched, and nothing that
         * points at this context survives the free. */
        for (int i = 0; i < ctx->participant_count; i++) {
            uvrpc_promise_t* participant = ctx->participants[i];
            if (participant && participant->callback == on_promise_race_callback
                && participant->callback_data == ctx) {
                participant->callback = promise_race_noop;
                participant->callback_data = NULL;
            }
        }

        UVRPC_FREE(ctx->participants);
        UVRPC_FREE(ctx);
        return;
    }

    /* Unreachable on the single-threaded dispatch path: the loop above swaps
     * every participant that still runs this callback before releasing the
     * context, so a second call here would mean reaching a context that is
     * already gone. Left as a no-op rather than a countdown, because counting
     * a field that no longer exists is the exact failure being removed. */
}

/* Promise.race() - Wait for first promise to complete */
int uvrpc_promise_race(uvrpc_promise_t** promises, int count, uvrpc_promise_t* combined, uv_loop_t* loop) {
    if (!promises || count <= 0 || !combined || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Allocate context */
    promise_race_context_t* ctx = (promise_race_context_t*)UVRPC_MALLOC(sizeof(promise_race_context_t));
    if (!ctx) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    ctx->combined = combined;
    ctx->completed = 0;
    ctx->loop = loop;

    /* Own the array. on_promise_race_callback() has to reach every participant
     * once the race is decided, and the caller's array is not ours to assume
     * anything about after this returns. */
    ctx->participants = (uvrpc_promise_t**)UVRPC_MALLOC(count * sizeof(uvrpc_promise_t*));
    if (!ctx->participants) {
        UVRPC_FREE(ctx);
        return UVRPC_ERROR_NO_MEMORY;
    }
    memcpy(ctx->participants, promises, count * sizeof(uvrpc_promise_t*));
    ctx->participant_count = count;

    /* Set callbacks for all promises */
    for (int i = 0; i < count; i++) {
        uvrpc_promise_set_callback(promises[i], on_promise_race_callback, ctx);
    }

    return UVRPC_OK;
}

/* Callback for Promise.allSettled() individual promise */
static void on_promise_all_settled_callback(uvrpc_promise_t* promise, void* user_data) {
    promise_all_settled_context_t* ctx = (promise_all_settled_context_t*)user_data;
    
    /* Get result or error */
    if (uvrpc_promise_is_fulfilled(promise)) {
        ctx->statuses[ctx->completed_count] = 1; /* fulfilled */
        
        uint8_t* result = NULL;
        size_t result_size = 0;
        int ret = uvrpc_promise_get_result(promise, &result, &result_size);
        if (ret == UVRPC_OK) {
            ctx->results[ctx->completed_count] = UVRPC_MALLOC(result_size);
            if (ctx->results[ctx->completed_count]) {
                memcpy(ctx->results[ctx->completed_count], result, result_size);
                ctx->result_sizes[ctx->completed_count] = result_size;
            }
            ctx->errors[ctx->completed_count] = NULL;
            ctx->error_codes[ctx->completed_count] = 0;
        }
    } else if (uvrpc_promise_is_rejected(promise)) {
        ctx->statuses[ctx->completed_count] = 2; /* rejected */
        
        const char* error = uvrpc_promise_get_error(promise);
        int32_t error_code = uvrpc_promise_get_error_code(promise);
        
        if (error) {
            size_t error_len = strlen(error) + 1;
            ctx->errors[ctx->completed_count] = (char*)UVRPC_MALLOC(error_len);
            if (ctx->errors[ctx->completed_count]) {
                strcpy(ctx->errors[ctx->completed_count], error);
            }
        }
        ctx->error_codes[ctx->completed_count] = error_code;
    } else {
        ctx->statuses[ctx->completed_count] = 0; /* pending */
    }
    
    /* Increment completed count */
    int completed = ++ctx->completed_count;
    
    /* If all completed, resolve combined promise */
    if (completed == ctx->total_count) {
        /* Create combined result with all settled data */
        size_t total_size = sizeof(int) + ctx->total_count * sizeof(uint8_t);
        
        /* Calculate total size needed */
        for (int i = 0; i < ctx->total_count; i++) {
            if (ctx->statuses[i] == 1) { /* fulfilled */
                total_size += sizeof(size_t) + ctx->result_sizes[i];
            } else if (ctx->statuses[i] == 2) { /* rejected */
                total_size += sizeof(int32_t);
                if (ctx->errors[i]) {
                    total_size += strlen(ctx->errors[i]) + 1;
                }
            }
        }
        
        uint8_t* combined_result = UVRPC_MALLOC(total_size);
        if (combined_result) {
            uint8_t* ptr = combined_result;
            
            /* Write count */
            memcpy(ptr, &ctx->total_count, sizeof(int));
            ptr += sizeof(int);
            
            /* Write each promise's settled status */
            for (int i = 0; i < ctx->total_count; i++) {
                memcpy(ptr, &ctx->statuses[i], sizeof(uint8_t));
                ptr += sizeof(uint8_t);
                
                if (ctx->statuses[i] == 1) { /* fulfilled */
                    memcpy(ptr, &ctx->result_sizes[i], sizeof(size_t));
                    ptr += sizeof(size_t);
                    if (ctx->results[i]) {
                        memcpy(ptr, ctx->results[i], ctx->result_sizes[i]);
                        ptr += ctx->result_sizes[i];
                    }
                } else if (ctx->statuses[i] == 2) { /* rejected */
                    memcpy(ptr, &ctx->error_codes[i], sizeof(int32_t));
                    ptr += sizeof(int32_t);
                    if (ctx->errors[i]) {
                        size_t error_len = strlen(ctx->errors[i]) + 1;
                        memcpy(ptr, ctx->errors[i], error_len);
                        ptr += error_len;
                    }
                }
            }
            
            uvrpc_promise_resolve(ctx->combined, combined_result, total_size);
            UVRPC_FREE(combined_result);
        } else {
            uvrpc_promise_reject(ctx->combined, UVRPC_ERROR_NO_MEMORY, "Failed to allocate combined result");
        }
        
        /* Cleanup context */
        for (int i = 0; i < ctx->total_count; i++) {
            if (ctx->results[i]) {
                UVRPC_FREE(ctx->results[i]);
            }
            if (ctx->errors[i]) {
                UVRPC_FREE(ctx->errors[i]);
            }
        }
        UVRPC_FREE(ctx->results);
        UVRPC_FREE(ctx->result_sizes);
        UVRPC_FREE(ctx->errors);
        UVRPC_FREE(ctx->error_codes);
        UVRPC_FREE(ctx->statuses);
        UVRPC_FREE(ctx);
    }
}

/* Promise.allSettled() - Wait for all promises to complete */
int uvrpc_promise_all_settled(uvrpc_promise_t** promises, int count, uvrpc_promise_t* combined, uv_loop_t* loop) {
    if (!promises || count <= 0 || !combined || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Allocate context */
    promise_all_settled_context_t* ctx = (promise_all_settled_context_t*)UVRPC_MALLOC(sizeof(promise_all_settled_context_t));
    if (!ctx) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    ctx->combined = combined;
    ctx->total_count = count;
    ctx->completed_count = 0;
    ctx->loop = loop;
    
    /* Allocate arrays */
    ctx->results = (uint8_t**)UVRPC_MALLOC(count * sizeof(uint8_t*));
    ctx->result_sizes = (size_t*)UVRPC_MALLOC(count * sizeof(size_t));
    ctx->errors = (char**)UVRPC_MALLOC(count * sizeof(char*));
    ctx->error_codes = (int32_t*)UVRPC_MALLOC(count * sizeof(int32_t));
    ctx->statuses = (uint8_t*)UVRPC_MALLOC(count * sizeof(uint8_t));
    
    if (!ctx->results || !ctx->result_sizes || !ctx->errors || !ctx->error_codes || !ctx->statuses) {
        if (ctx->results) UVRPC_FREE(ctx->results);
        if (ctx->result_sizes) UVRPC_FREE(ctx->result_sizes);
        if (ctx->errors) UVRPC_FREE(ctx->errors);
        if (ctx->error_codes) UVRPC_FREE(ctx->error_codes);
        if (ctx->statuses) UVRPC_FREE(ctx->statuses);
        UVRPC_FREE(ctx);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    memset(ctx->results, 0, count * sizeof(uint8_t*));
    memset(ctx->result_sizes, 0, count * sizeof(size_t));
    memset(ctx->errors, 0, count * sizeof(char*));
    memset(ctx->error_codes, 0, count * sizeof(int32_t));
    memset(ctx->statuses, 0, count * sizeof(uint8_t));
    
    /* Set callbacks for all promises */
    for (int i = 0; i < count; i++) {
        uvrpc_promise_set_callback(promises[i], on_promise_all_settled_callback, ctx);
    }
    
    return UVRPC_OK;
}

/* The body shared by uvrpc_promise_all_sync() and uvrpc_promise_race_sync().
 *
 * The combined promise is heap-allocated rather than a local, and released
 * through uvrpc_promise_destroy(). It holds a libuv async handle, and a handle
 * can only be given back by uv_close(), which merely queues the close -- so
 * the struct has to still be there when the loop runs it. On the stack it was
 * not: the function returned, its frame died, and the next uv_run() read the
 * dead frame. Measured, with two already-resolved promises and a plain call:
 *
 *     all_sync -> UVRPC_OK
 *     then uv_run() -> SIGSEGV
 *
 * Which combinator to run is passed in rather than branched on, because the
 * two sync wrappers are otherwise identical. */
static int promise_combine_sync(uvrpc_promise_t** promises, int count,
                                uint8_t** result, size_t* result_size,
                                uv_loop_t* loop,
                                int (*combine)(uvrpc_promise_t**, int,
                                               uvrpc_promise_t*, uv_loop_t*)) {
    uvrpc_promise_t* combined = uvrpc_promise_create(loop);
    if (!combined) {
        return UVRPC_ERROR_NO_MEMORY;
    }

    int ret = combine(promises, count, combined, loop);
    if (ret != UVRPC_OK) {
        uvrpc_promise_destroy(combined);
        return ret;
    }

    /* Wait for completion */
    ret = uvrpc_promise_wait(combined);

    /* Get result and transfer ownership to caller */
    if (ret == UVRPC_OK && result && result_size) {
        uint8_t* internal_result = NULL;
        size_t internal_size = 0;
        uvrpc_promise_get_result(combined, &internal_result, &internal_size);

        /* Copy result to transfer ownership to caller */
        if (internal_result && internal_size > 0) {
            *result = (uint8_t*)UVRPC_MALLOC(internal_size);
            if (*result) {
                memcpy(*result, internal_result, internal_size);
                *result_size = internal_size;
            } else {
                ret = UVRPC_ERROR_NO_MEMORY;
            }
        } else {
            *result = NULL;
            *result_size = 0;
        }
    }

    /* Releases the payload now and the struct on the loop's next close. The
     * copy above is what makes that safe: the caller's bytes are its own. */
    uvrpc_promise_destroy(combined);

    return ret;
}

/* Promise.all() - Synchronous version (blocking) */
int uvrpc_promise_all_sync(uvrpc_promise_t** promises, int count,
                        uint8_t** result, size_t* result_size,
                        uv_loop_t* loop) {
    if (!promises || count <= 0 || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    return promise_combine_sync(promises, count, result, result_size, loop,
                                uvrpc_promise_all);
}

/* Promise.race() - Synchronous version (blocking) */
int uvrpc_promise_race_sync(uvrpc_promise_t** promises, int count,
                        uint8_t** result, size_t* result_size,
                        uv_loop_t* loop) {
    if (!promises || count <= 0 || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    return promise_combine_sync(promises, count, result, result_size, loop,
                                uvrpc_promise_race);
}