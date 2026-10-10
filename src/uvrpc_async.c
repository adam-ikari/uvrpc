/**
 * @file uvrpc_async.c
 * @brief UVRPC Async/Await Implementation
 * 
 * Provides coroutine-like async/await syntax by running the event loop inline.
 *
 * The blocking await used to be built on setjmp/longjmp: the response callback
 * longjmp'd straight back into the waiting call. That unwound the C stack out
 * of libuv, past the transport's own cleanup, so every await over a socket
 * transport leaked the read buffer libuv had just allocated. The wait is now an
 * ordinary loop -- issue the call, then uv_run() until the result is in place --
 * and callbacks end it by stopping the loop.
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 */

#include "../include/uvrpc_async.h"
#include "../include/uvrpc_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define UVRPC_MAX_ASYNC_CALLS 100
#define UVRPC_MAX_CONCURRENT_CALLS 20

/* A backoff delay between retry attempts.
 *
 * Used to be a retry_context_t holding the client, method and params, with the
 * uv_timer_t embedded in it, and the timer callback did the retry and then freed
 * the struct -- the one containing the timer that was running it. Three defects
 * came out of that shape, all measured:
 *
 *   - the handle was never uv_close()d, so after any retry the loop could never
 *     be closed: uv_loop_close() returned UV_EBUSY (-16) permanently;
 *   - the callback fired once and freed itself, so `max_retries` did nothing.
 *     A server that failed four times before succeeding was still served only
 *     twice -- one call plus one retry -- no matter how many retries were asked
 *     for;
 *   - it returned UVRPC_ERROR to mean "a retry is scheduled", which the caller
 *     cannot distinguish from failure. That is why concurrent_demo has always
 *     printed "[Async] Retry failed" against a server that answers every time.
 *
 * Retrying is now inline in the retry functions, where the attempt counter lives
 * and where the delay can be waited for the same way every other wait in this
 * file is: run the loop until the timer says so. No context to free, no handle
 * to leak, and the return value means what it says. */
typedef struct {
    uv_timer_t timer;
    int elapsed;
} backoff_delay_t;

static void backoff_elapsed(uv_timer_t* handle) {
    backoff_delay_t* d = (backoff_delay_t*)handle->data;
    d->elapsed = 1;
    /* libuv 1.47 has no uv_timer_get_loop(); a uv_timer_t is a uv_handle_t as
     * far as the public API is concerned. */
    uv_stop(uv_handle_get_loop((uv_handle_t*)handle));
}

static void backoff_closed(uv_handle_t* handle) {
    uvrpc_free(handle);
}

/* Async context structure.
 *
 * pending_results[] is the awaited call's single slot array. It is not a queue:
 * an awaited call blocks until its result is in place, takes it, and clears the
 * slot, so the array is empty between calls. That is why there is no "pending
 * count" here to report -- see the note on uvrpc_async_cancel_all(). */
struct uvrpc_async_ctx {
    uv_loop_t* loop;
    int is_running;
    uvrpc_async_result_t* pending_results[UVRPC_MAX_ASYNC_CALLS];
    int current_call;
    /* Set when the response could not be copied out, so the wait reports a
     * failure instead of timing out with nothing to show. */
    int out_of_memory;
    uint64_t timeout_ms;
    /* Heap-allocated, not embedded. uvrpc_async_ctx_free() must be able to run
     * with the timer still armed -- an awaited call can succeed long before the
     * deadline -- and uv_close() only queues the close, so a handle inside this
     * struct would still be on the loop's queue after the struct was freed.
     * Measured: freeing a context whose 60 ms timer had not expired, then
     * letting the loop run, wrote through the freed memory and took the
     * process down. */
    uv_timer_t* timeout_timer;
    int timed_out;

    /* For concurrent operations */
    int concurrent_count;
    int completed_count;
    uvrpc_async_result_t* concurrent_results[UVRPC_MAX_CONCURRENT_CALLS];
    int any_completed;
};

/* End the wait that uvrpc_client_call_async() is running.
 *
 * This used to be a longjmp straight out of the callback, which unwound the C
 * stack through libuv: on the socket transports the response arrives inside
 * uv__read(), and jumping out of it skipped the transport's `uvrpc_free(buf->base)`
 * at the end of on_client_read(). Measured: every await over TCP, IPC or UDP
 * leaked the 64 KB read buffer libuv had just handed the transport. It also
 * skipped the callback's own tail, so nothing else in that path could be relied
 * on either.
 *
 * Stopping the loop instead leaves the callback to return normally: libuv
 * finishes the read, the transport frees its buffer, and the wait in
 * uvrpc_client_call_async() ends because uv_run() came back and the loop
 * condition was re-checked. */
static void wake_waiter(uvrpc_async_ctx_t* ctx) {
    if (ctx->loop) {
        uv_stop(ctx->loop);
    }
}

/* Internal wrapper for response callback */
static void async_response_callback(uvrpc_response_t* resp, void* ctx) {
    uvrpc_async_ctx_t* async_ctx = (uvrpc_async_ctx_t*)ctx;
    
    if (async_ctx->current_call < 0 || async_ctx->current_call >= UVRPC_MAX_ASYNC_CALLS) {
        return;
    }
    
    /* Store result */
    uvrpc_async_result_t* result = uvrpc_alloc(sizeof(uvrpc_async_result_t));
    if (!result) {
        /* Reported through the same flag the wait loop checks, so an allocation
         * failure surfaces as UVRPC_ERROR_NO_MEMORY rather than as success. */
        async_ctx->out_of_memory = 1;
        wake_waiter(async_ctx);
        return;
    }
    
    result->status = resp->status;
    result->msgid = resp->msgid;
    result->error_code = resp->error_code;
    result->user_data = resp->user_data;
    
    if (resp->result && resp->result_size > 0) {
        result->result = uvrpc_alloc(resp->result_size);
        if (result->result) {
            memcpy(result->result, resp->result, resp->result_size);
            result->result_size = resp->result_size;
        } else {
            result->result_size = 0;
        }
    } else {
        result->result = NULL;
        result->result_size = 0;
    }
    
    async_ctx->pending_results[async_ctx->current_call] = result;

    /* The result is in place; end the wait so the caller sees it. */
    wake_waiter(async_ctx);
}

/* Timeout callback */
static void timeout_callback(uv_timer_t* handle) {
    uvrpc_async_ctx_t* async_ctx = (uvrpc_async_ctx_t*)handle->data;
    async_ctx->timed_out = 1;
    wake_waiter(async_ctx);
}

/* Frees the timer handle once libuv has finished closing it. The handle is
 * heap-allocated rather than embedded in the context precisely so this can
 * happen: uv_close() only queues the close, so the memory has to outlive the
 * call that asked for it. */
static void timeout_timer_closed(uv_handle_t* handle) {
    uvrpc_free(handle);
}

/* Create async context */
uvrpc_async_ctx_t* uvrpc_async_ctx_new(uv_loop_t* loop) {
    if (!loop) return NULL;
    
    uvrpc_async_ctx_t* ctx = uvrpc_alloc(sizeof(uvrpc_async_ctx_t));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(uvrpc_async_ctx_t));
    ctx->loop = loop;
    ctx->is_running = 0;
    ctx->current_call = -1;
    ctx->timeout_ms = 0;
    ctx->timed_out = 0;
    
    return ctx;
}

/* Free async context */
void uvrpc_async_ctx_free(uvrpc_async_ctx_t* ctx) {
    if (!ctx) return;
    
    /* Free pending results */
    for (int i = 0; i < UVRPC_MAX_ASYNC_CALLS; i++) {
        if (ctx->pending_results[i]) {
            uvrpc_async_result_free(ctx->pending_results[i]);
            ctx->pending_results[i] = NULL;
        }
    }
    
    /* The timeout timer may still be armed -- a call that succeeded on its
     * first response leaves the deadline running. Stop it, then hand it to
     * libuv to close; the close callback frees it, which is why it is not part
     * of this allocation. The context's pointer is cleared first so a timer
     * that is already inside its callback cannot reach the freed context. */
    if (ctx->timeout_timer) {
        uv_timer_t* timer = ctx->timeout_timer;
        ctx->timeout_timer = NULL;
        ctx->timed_out = 1;
        uv_timer_stop(timer);
        uv_close((uv_handle_t*)timer, timeout_timer_closed);
    }
    
    uvrpc_free(ctx);
}

/* Execute async block */
int uvrpc_async_exec(uvrpc_async_ctx_t* ctx, uint64_t timeout_ms) {
    if (!ctx || ctx->is_running) return UVRPC_ERROR_INVALID_PARAM;
    
    ctx->is_running = 1;
    ctx->timeout_ms = timeout_ms;
    ctx->timed_out = 0;
    
    /* Start timeout timer if specified */
    if (timeout_ms > 0) {
        /* Re-arm rather than re-init: uv_timer_init() on a live handle is a
         * second initialisation of the same memory, and this context can be
         * re-entered by a second UVRPC_ASYNC block. */
        if (!ctx->timeout_timer) {
            ctx->timeout_timer = (uv_timer_t*)uvrpc_alloc(sizeof(uv_timer_t));
            if (!ctx->timeout_timer) {
                ctx->is_running = 0;
                return UVRPC_ERROR_NO_MEMORY;
            }
            if (uv_timer_init(ctx->loop, ctx->timeout_timer) != 0) {
                uvrpc_free(ctx->timeout_timer);
                ctx->timeout_timer = NULL;
                ctx->is_running = 0;
                return UVRPC_ERROR;
            }
            ctx->timeout_timer->data = ctx;
        } else {
            uv_timer_stop(ctx->timeout_timer);
        }
        uv_timer_start(ctx->timeout_timer, timeout_callback, timeout_ms, 0);
    }
    
    return UVRPC_OK;
}

/* Client call with async/await */
int uvrpc_client_call_async(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                             const char* method, const uint8_t* params,
                             size_t params_size, uvrpc_async_result_t** result) {
    if (!ctx || !client || !method || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (!ctx->is_running) {
        return UVRPC_ERROR;
    }
    
    /* Find free slot for result */
    int slot = -1;
    for (int i = 0; i < UVRPC_MAX_ASYNC_CALLS; i++) {
        if (ctx->pending_results[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    /* Set current call index */
    ctx->current_call = slot;
    ctx->out_of_memory = 0;

    int call_status = uvrpc_client_call(client, method, params, params_size,
                                         async_response_callback, ctx);
    if (call_status != UVRPC_OK) {
        ctx->current_call = -1;
        return call_status;
    }

    /* Run the loop until the response lands, the deadline expires, or there is
     * nothing left that could produce either.
     *
     * UV_RUN_ONCE, not UV_RUN_DEFAULT. DEFAULT does not return until the loop
     * has no referenced handles left, and a connected client holds one for as
     * long as it is connected -- so over any socket transport the loop
     * condition below would never be re-checked. ONCE returns after each batch
     * of events. The INPROC demos worked either way because that transport
     * holds no socket handle, which is why the defect survived.
     *
     * ran == 0 means the loop has nothing left to do, so the response cannot
     * arrive; continuing would spin. */
    while (ctx->pending_results[slot] == NULL && !ctx->timed_out &&
           !ctx->out_of_memory) {
        if (uv_run(ctx->loop, UV_RUN_ONCE) == 0) {
            break;
        }
    }

    ctx->current_call = -1;

    if (ctx->out_of_memory) {
        ctx->out_of_memory = 0;
        return UVRPC_ERROR_NO_MEMORY;
    }

    if (ctx->pending_results[slot] == NULL) {
        /* Stop the timer but leave the handle open: it belongs to the context
         * and is released by uvrpc_async_ctx_free(). */
        if (ctx->timeout_timer) {
            uv_timer_stop(ctx->timeout_timer);
        }
        return UVRPC_ERROR_TIMEOUT;
    }

    *result = ctx->pending_results[slot];
    ctx->pending_results[slot] = NULL;

    return UVRPC_OK;
}

/* Free async result */
void uvrpc_async_result_free(uvrpc_async_result_t* result) {
    if (!result) return;
    
    if (result->result) {
        uvrpc_free(result->result);
    }
    
    uvrpc_free(result);
}

/* Callback for concurrent operations */
static void concurrent_callback(uvrpc_response_t* resp, void* ctx) {
    uvrpc_async_ctx_t* async_ctx = (uvrpc_async_ctx_t*)ctx;
    int index = (int)(intptr_t)resp->user_data;
    
    /* Store result */
    uvrpc_async_result_t* result = uvrpc_alloc(sizeof(uvrpc_async_result_t));
    if (!result) return;
    
    result->status = resp->status;
    result->msgid = resp->msgid;
    result->error_code = resp->error_code;
    
    if (resp->result && resp->result_size > 0) {
        result->result = uvrpc_alloc(resp->result_size);
        if (result->result) {
            memcpy(result->result, resp->result, resp->result_size);
            result->result_size = resp->result_size;
        } else {
            result->result_size = 0;
        }
    } else {
        result->result = NULL;
        result->result_size = 0;
    }
    
    async_ctx->concurrent_results[index] = result;
    async_ctx->completed_count++;

    /* For any(): the first one to land ends the wait. Stopping the loop rather
     * than longjmp-ing for the reason on wake_waiter -- this callback also runs
     * inside libuv's read path. */
    if (!async_ctx->any_completed) {
        async_ctx->any_completed = 1;
        wake_waiter(async_ctx);
    }
}

/* A one-shot deadline for the concurrent combinators.
 *
 * Heap-allocated with the flag beside the handle, and released by the close
 * callback. These used to be a bare `uv_timer_t` on the stack with a separate
 * `int timed_out`: uv_close() only queues the close, so the loop touched that
 * dead frame on its next iteration. */
typedef struct {
    uv_timer_t timer;
    int timed_out;
} combinator_deadline_t;

static void deadline_fired(uv_timer_t* handle) {
    combinator_deadline_t* d = (combinator_deadline_t*)handle->data;
    d->timed_out = 1;
}

static void deadline_closed(uv_handle_t* handle) {
    uvrpc_free(handle);
}

/* Returns NULL when no timeout was asked for, which is the caller's signal to
 * wait indefinitely. Also NULL on allocation failure -- an allocation failure
 * cannot be reported through the int return these functions already use for
 * UVRPC_ERROR_TIMEOUT, so it degrades to the no-deadline behaviour. */
static combinator_deadline_t* deadline_start(uv_loop_t* loop, uint64_t timeout_ms) {
    if (timeout_ms == 0) {
        return NULL;
    }
    combinator_deadline_t* d =
        (combinator_deadline_t*)uvrpc_alloc(sizeof(combinator_deadline_t));
    if (!d) {
        return NULL;
    }
    if (uv_timer_init(loop, &d->timer) != 0) {
        uvrpc_free(d);
        return NULL;
    }
    d->timed_out = 0;
    d->timer.data = d;
    uv_timer_start(&d->timer, deadline_fired, timeout_ms, 0);
    return d;
}

/* Read the flag before stopping: after deadline_stop() the struct is queued for
 * free, so touching it again is a use-after-free waiting for the next loop
 * iteration. */
static int deadline_stop(combinator_deadline_t* d) {
    if (!d) {
        return 0;
    }
    int timed_out = d->timed_out;
    uv_timer_stop(&d->timer);
    uv_close((uv_handle_t*)&d->timer, deadline_closed);
    return timed_out;
}

/* Promise.all - wait for all concurrent calls to complete */
int uvrpc_async_all(uvrpc_async_ctx_t* ctx,
                     uvrpc_client_t** clients,
                     const char** methods,
                     const uint8_t** params_array,
                     size_t* params_sizes,
                     uvrpc_async_result_t*** results,
                     int count,
                     uint64_t timeout_ms) {
    if (!ctx || !clients || !methods || !params_array || !params_sizes || !results) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (count <= 0 || count > UVRPC_MAX_CONCURRENT_CALLS) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Initialize concurrent state */
    ctx->concurrent_count = count;
    ctx->completed_count = 0;
    ctx->any_completed = 0;
    for (int i = 0; i < count; i++) {
        ctx->concurrent_results[i] = NULL;
    }
    
    /* Setup timeout */
    combinator_deadline_t* deadline = deadline_start(ctx->loop, timeout_ms);

    /* Start all concurrent calls */
    for (int i = 0; i < count; i++) {
        uvrpc_response_t* resp_wrapper = uvrpc_alloc(sizeof(uvrpc_response_t));
        resp_wrapper->user_data = (void*)(intptr_t)i;
        
        uvrpc_client_call(clients[i], methods[i], params_array[i], params_sizes[i],
                          concurrent_callback, ctx);
    }
    
    /* Wait for all to complete. UV_RUN_ONCE returns after each batch of
     * events; DEFAULT would not return at all while any client is connected. */
    while (ctx->completed_count < count && !(deadline && deadline->timed_out)) {
        if (uv_run(ctx->loop, UV_RUN_ONCE) == 0) {
            break;
        }
    }

    int timed_out = deadline_stop(deadline);

    if (timed_out) {
        return UVRPC_ERROR_TIMEOUT;
    }
    
    /* Allocate results array */
    *results = uvrpc_alloc(sizeof(uvrpc_async_result_t*) * count);
    if (!*results) {
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    for (int i = 0; i < count; i++) {
        (*results)[i] = ctx->concurrent_results[i];
        ctx->concurrent_results[i] = NULL;
    }
    
    return UVRPC_OK;
}

/* Promise.any - wait for any one concurrent call to complete */
int uvrpc_async_any(uvrpc_async_ctx_t* ctx,
                     uvrpc_client_t** clients,
                     const char** methods,
                     const uint8_t** params_array,
                     size_t* params_sizes,
                     uvrpc_async_result_t** result,
                     int* completed_index,
                     int count,
                     uint64_t timeout_ms) {
    if (!ctx || !clients || !methods || !params_array || !params_sizes || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    if (count <= 0 || count > UVRPC_MAX_CONCURRENT_CALLS) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Initialize concurrent state */
    ctx->concurrent_count = count;
    ctx->completed_count = 0;
    ctx->any_completed = 0;
    for (int i = 0; i < count; i++) {
        ctx->concurrent_results[i] = NULL;
    }
    
    /* Setup timeout */
    combinator_deadline_t* deadline = deadline_start(ctx->loop, timeout_ms);

    /* Start all concurrent calls */
    for (int i = 0; i < count; i++) {
        uvrpc_client_call(clients[i], methods[i], params_array[i], params_sizes[i],
                          concurrent_callback, ctx);
    }

    /* Wait for any to complete. UV_RUN_ONCE, not DEFAULT -- see the note in
     * uvrpc_client_call_async(): DEFAULT does not return while a client
     * connection is still open. */
    while (!ctx->any_completed && !(deadline && deadline->timed_out)) {
        if (uv_run(ctx->loop, UV_RUN_ONCE) == 0) {
            break;
        }
    }

    /* The deadline is released exactly once, whichever way this left. */
    int timed_out = deadline_stop(deadline);
    if (timed_out) {
        return UVRPC_ERROR_TIMEOUT;
    }

    /* Find completed result */
    for (int i = 0; i < count; i++) {
        if (ctx->concurrent_results[i]) {
            *result = ctx->concurrent_results[i];
            *completed_index = i;
            ctx->concurrent_results[i] = NULL;
            return UVRPC_OK;
        }
    }

    /* Nothing completed and no deadline expired: the loop ran out of work. */
    return UVRPC_ERROR_TIMEOUT;
}

/* Run the loop for `delay_ms`. Returns 0 if the delay could not be set up, in
 * which case the caller should give up rather than retry immediately -- a retry
 * with no delay is a hot loop against a server that is already failing. */
static int wait_backoff(uv_loop_t* loop, uint64_t delay_ms) {
    if (delay_ms == 0) {
        return 1;
    }

    backoff_delay_t* d = (backoff_delay_t*)uvrpc_alloc(sizeof(backoff_delay_t));
    if (!d) {
        return 0;
    }
    if (uv_timer_init(loop, &d->timer) != 0) {
        uvrpc_free(d);
        return 0;
    }
    d->elapsed = 0;
    d->timer.data = d;
    uv_timer_start(&d->timer, backoff_elapsed, delay_ms, 0);

    while (!d->elapsed) {
        if (uv_run(loop, UV_RUN_ONCE) == 0) {
            break;  /* nothing left to run: the timer cannot fire */
        }
    }

    /* Stop and close, and let the close callback free it: uv_close() only queues
     * the work, so freeing here would leave the handle on the loop's queue. */
    uv_timer_stop(&d->timer);
    uv_close((uv_handle_t*)&d->timer, backoff_closed);
    return d->elapsed;
}

/* How long to wait before each attempt. Two policies share one loop: a fixed
 * delay, and exponential backoff. Returning 0 means do not retry. */
typedef uint64_t (*retry_delay_fn)(int attempt, void* user);

static uint64_t retry_constant_delay(int attempt, void* user) {
    (void)attempt;
    return *(uint64_t*)user;
}

/* The shared retry loop.
 *
 * The status returned is the one the caller cares about: UVRPC_OK only when a
 * call completed AND carried no error code. uvrpc_client_call_async() returns
 * UVRPC_OK for a call the server answered with an error frame, so taking its
 * return value on its own would report success for a service that is failing. */
static int retry_loop(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                      const char* method, const uint8_t* params,
                      size_t params_size, uvrpc_async_result_t** result,
                      int max_retries, retry_delay_fn delay_for, void* user) {
    /* What to report if every attempt fails. Kept as two values because a
     * rejected call and an answered-but-erroneous one are different pieces of
     * information and the caller needs the more specific one. */
    int last_call_status = UVRPC_ERROR;
    int32_t last_error_code = 0;

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int call_status = uvrpc_client_call_async(ctx, client, method, params,
                                                  params_size, result);

        if (call_status == UVRPC_OK && *result) {
            if ((*result)->error_code == 0) {
                return UVRPC_OK;
            }
            /* The call worked; the service answered with an error. */
            last_call_status = UVRPC_OK;
            last_error_code = (*result)->error_code;
        } else {
            last_call_status = call_status;
            last_error_code = 0;
        }

        if (*result) {
            uvrpc_async_result_free(*result);
            *result = NULL;
        }

        if (attempt == max_retries) {
            break;
        }

        uint64_t delay_ms = delay_for(attempt, user);
        if (delay_ms == 0) {
            break;  /* no retry was wanted */
        }
        if (!wait_backoff(ctx->loop, delay_ms)) {
            /* The delay could not be set up, or the loop had nothing left to
             * run it on. Stop rather than hammer the remaining attempts. */
            break;
        }
    }

    if (last_call_status != UVRPC_OK) {
        return last_call_status;
    }
    return last_error_code != 0 ? last_error_code : UVRPC_ERROR;
}

/* Promise.retry - retry failed calls */
int uvrpc_async_retry(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                       const char* method, const uint8_t* params,
                       size_t params_size, uvrpc_async_result_t** result,
                       int max_retries, uint64_t retry_delay_ms) {
    if (!ctx || !client || !method || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (max_retries < 0) max_retries = 3;

    /* The context has to be in a running block: uvrpc_client_call_async()
     * rejects the call otherwise, and a rejected call is not a server failure,
     * so retrying it would just burn the attempts. */
    if (!ctx->is_running) {
        return UVRPC_ERROR_INVALID_STATE;
    }

    return retry_loop(ctx, client, method, params, params_size, result,
                      max_retries, retry_constant_delay, &retry_delay_ms);
}
int uvrpc_async_timeout(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                         const char* method, const uint8_t* params,
                         size_t params_size, uvrpc_async_result_t** result,
                         uint64_t timeout_ms) {
    if (!ctx || !client || !method || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Setup timeout. Same handle uvrpc_async_exec() arms: re-arm it rather than
     * uv_timer_init()ing it again, which would initialise live memory a second
     * time. */
    if (timeout_ms > 0) {
        if (!ctx->timeout_timer) {
            ctx->timeout_timer = (uv_timer_t*)uvrpc_alloc(sizeof(uv_timer_t));
            if (!ctx->timeout_timer) {
                return UVRPC_ERROR_NO_MEMORY;
            }
            if (uv_timer_init(ctx->loop, ctx->timeout_timer) != 0) {
                uvrpc_free(ctx->timeout_timer);
                ctx->timeout_timer = NULL;
                return UVRPC_ERROR;
            }
            ctx->timeout_timer->data = ctx;
        } else {
            uv_timer_stop(ctx->timeout_timer);
        }
        ctx->timed_out = 0;
        uv_timer_start(ctx->timeout_timer, timeout_callback, timeout_ms, 0);
    }

    /* Make the call. It runs its own wait and reports the deadline itself, so
     * there is nothing to jump back to here. */
    return uvrpc_client_call_async(ctx, client, method, params, params_size, result);
}

/* The exponential-backoff delay policy for retry_loop(). */
typedef struct {
    uint64_t initial_delay_ms;
    double multiplier;
} backoff_policy_t;

static uint64_t backoff_delay(int attempt, void* user) {
    backoff_policy_t* p = (backoff_policy_t*)user;
    /* attempt here is the number of failures so far, so the first delay is the
     * initial one. */
    double factor = pow(p->multiplier, (double)attempt);
    return (uint64_t)((double)p->initial_delay_ms * factor);
}

/* Promise.retry with exponential backoff */
int uvrpc_async_retry_with_backoff(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                                   const char* method, const uint8_t* params,
                                   size_t params_size, uvrpc_async_result_t** result,
                                   int max_retries, uint64_t initial_delay_ms,
                                   double backoff_multiplier) {
    if (!ctx || !client || !method || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    if (max_retries < 0) max_retries = 3;
    if (initial_delay_ms == 0) initial_delay_ms = 100;
    if (backoff_multiplier < 1.0) backoff_multiplier = 2.0;

    /* See uvrpc_async_retry(): a context outside a running block would have
     * every attempt rejected for a reason retrying cannot fix. */
    if (!ctx->is_running) {
        return UVRPC_ERROR_INVALID_STATE;
    }

    backoff_policy_t policy;
    policy.initial_delay_ms = initial_delay_ms;
    policy.multiplier = backoff_multiplier;

    return retry_loop(ctx, client, method, params, params_size, result,
                      max_retries, backoff_delay, &policy);
}

/* uvrpc_async_race() -- declared in the header with no definition anywhere in
 * the library until now. Same signature and behaviour as uvrpc_async_any(). */
int uvrpc_async_race(uvrpc_async_ctx_t* ctx,
                     uvrpc_client_t** clients,
                     const char** methods,
                     const uint8_t** params_array,
                     size_t* params_sizes,
                     uvrpc_async_result_t** result,
                     int* completed_index,
                     int count,
                     uint64_t timeout_ms) {
    return uvrpc_async_any(ctx, clients, methods, params_array, params_sizes,
                           result, completed_index, count, timeout_ms);
}

/* Abandon the current async block: stop the deadline and reset it.
 *
 * This cannot cancel work in flight, and the pending_results sweep below cannot
 * either: an awaited call blocks until it has answered or its deadline expired,
 * so every slot is clear by the time a caller can reach this. The sweep is
 * kept only as teardown symmetry with uvrpc_async_ctx_free(), which has the
 * same loop over the same field.
 *
 * Do not call this from inside a response callback. That callback runs within
 * the wait loop, which is about to read the slot this would free. */
int uvrpc_async_cancel_all(uvrpc_async_ctx_t* ctx) {
    if (!ctx) return UVRPC_ERROR_INVALID_PARAM;

    /* Stop the timeout timer if it is armed. Do not close it: abandoning a block
     * is not the same as tearing the context down, and the next UVRPC_ASYNC
     * block re-arms this same handle. It is closed by uvrpc_async_ctx_free(). */
    if (ctx->timeout_timer) {
        uv_timer_stop(ctx->timeout_timer);
        ctx->timed_out = 1;
    }

    for (int i = 0; i < UVRPC_MAX_ASYNC_CALLS; i++) {
        if (ctx->pending_results[i]) {
            uvrpc_async_result_free(ctx->pending_results[i]);
            ctx->pending_results[i] = NULL;
        }
    }

    ctx->current_call = -1;
    ctx->is_running = 0;

    return UVRPC_OK;
}
