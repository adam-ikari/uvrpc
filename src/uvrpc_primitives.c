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
#include <unistd.h>

/* For atomic operations - GCC/Clang builtins */
#if defined(__GNUC__) || defined(__clang__)
#define UVRPC_ATOMIC_ADD(ptr, val) __sync_add_and_fetch(ptr, val)
#define UVRPC_ATOMIC_SUB(ptr, val) __sync_sub_and_fetch(ptr, val)
#define UVRPC_ATOMIC_LOAD(ptr) __sync_fetch_and_add(ptr, 0)
#define UVRPC_ATOMIC_STORE(ptr, val) __sync_lock_test_and_set(ptr, val)
#define UVRPC_ATOMIC_CAS(ptr, oldval, newval) __sync_val_compare_and_swap(ptr, oldval, newval)
#define UVRPC_ATOMIC_COMPARE_AND_SWAP(ptr, oldval, newval) __sync_bool_compare_and_swap(ptr, oldval, newval)
#else
#error "Atomic operations not supported for this compiler"
#endif

/* Use uvrpc_alloc/u vrpc_free for memory allocation */
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

/* Cleanup promise */
void uvrpc_promise_cleanup(uvrpc_promise_t* promise) {
    if (!promise) return;
    
    /* Close async handle if not closing */
    if (!uv_is_closing((uv_handle_t*)&promise->async_handle)) {
        uv_close((uv_handle_t*)&promise->async_handle, NULL);
    }
    
    /* Free result */
    if (promise->result) {
        uvrpc_free(promise->result);
        promise->result = NULL;
    }
    
    /* Free error message */
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

/* Cleanup and free a Promise (convenience function) */
void uvrpc_promise_destroy(uvrpc_promise_t* promise) {
    if (!promise) {
        return;
    }
    
    uvrpc_promise_cleanup(promise);
    UVRPC_FREE(promise);
}

/* Wait for Promise to complete synchronously (blocking) */
int uvrpc_promise_wait(uvrpc_promise_t* promise) {
    if (!promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Run event loop until promise is settled */
    while (promise->state == UVRPC_PROMISE_PENDING) {
        uv_run(promise->loop, UV_RUN_ONCE);
        usleep(1000);  /* 1ms sleep to avoid busy loop */
    }
    
    if (promise->state == UVRPC_PROMISE_FULFILLED) {
        return UVRPC_OK;
    } else {
        return promise->error_code;
    }
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
    if (!promise || !result || !result_size) {
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
typedef struct semaphore_waiter {
    uvrpc_promise_t* promise;
    struct semaphore_waiter* next;
} semaphore_waiter_t;

/* Global waiter queue (simplified - single semaphore for demo) */
/* In production, this should be per-semaphore with proper locking */
static semaphore_waiter_t* g_waiter_queue = NULL;
static uv_mutex_t g_waiter_mutex;

/* Initialize waiter mutex (called once) */
static void semaphore_init_mutex(void) {
    static int initialized = 0;
    if (!initialized) {
        uv_mutex_init(&g_waiter_mutex);
        initialized = 1;
    }
}

/* Async callback for semaphore */
static void semaphore_async_callback(uv_async_t* handle) {
    uvrpc_semaphore_t* semaphore = (uvrpc_semaphore_t*)handle->data;
    
    /* Process waiting queue */
    semaphore_init_mutex();
    uv_mutex_lock(&g_waiter_mutex);
    
    while (g_waiter_queue != NULL && semaphore->permits > 0) {
        semaphore_waiter_t* waiter = g_waiter_queue;
        g_waiter_queue = waiter->next;
        
        /* Acquire permit */
        semaphore->permits--;
        
        /* Check if this is a Promise-based waiter */
        if (waiter->promise) {
            /* Resolve the Promise (JavaScript-style) */
            int result = 1;
            uvrpc_promise_resolve(waiter->promise, (uint8_t*)&result, sizeof(int));
        }
        
        uvrpc_free(waiter);
    }
    
    uv_mutex_unlock(&g_waiter_mutex);
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
    
    int ret = uv_async_init(loop, &semaphore->async_handle, semaphore_async_callback);
    if (ret != 0) {
        return UVRPC_ERROR;
    }
    semaphore->async_handle.data = semaphore;
    uv_unref((uv_handle_t*)&semaphore->async_handle);
    
    semaphore_init_mutex();
    
    return UVRPC_OK;
}

/* Cleanup semaphore */
void uvrpc_semaphore_cleanup(uvrpc_semaphore_t* semaphore) {
    if (!semaphore) return;
    
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
    
    semaphore_init_mutex();
    uv_mutex_lock(&g_waiter_mutex);
    
    int has_waiters = (g_waiter_queue != NULL);
    
    /* Increment permit count */
    semaphore->permits++;
    
    uv_mutex_unlock(&g_waiter_mutex);
    
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
    
    semaphore_init_mutex();
    uv_mutex_lock(&g_waiter_mutex);
    
    if (semaphore->permits > 0) {
        semaphore->permits--;
        uv_mutex_unlock(&g_waiter_mutex);
        return 1;
    }
    
    uv_mutex_unlock(&g_waiter_mutex);
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
    
    semaphore_init_mutex();
    uv_mutex_lock(&g_waiter_mutex);
    
    if (semaphore->permits > 0) {
        /* Permit available immediately */
        semaphore->permits--;
        uv_mutex_unlock(&g_waiter_mutex);
        
        /* Resolve the promise */
        int result = 1;
        return uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
    }
    
    /* No permit available, queue the promise */
    /* Store promise in waiter queue */
    semaphore_waiter_t* waiter = (semaphore_waiter_t*)UVRPC_MALLOC(sizeof(semaphore_waiter_t));
    if (!waiter) {
        uv_mutex_unlock(&g_waiter_mutex);
        return UVRPC_ERROR_NO_MEMORY;
    }
    
    waiter->promise = promise;
    waiter->next = NULL;
    
    /* Add to queue */
    if (g_waiter_queue == NULL) {
        g_waiter_queue = waiter;
    } else {
        semaphore_waiter_t* tail = g_waiter_queue;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = waiter;
    }
    
    semaphore->waiting++;
    uv_mutex_unlock(&g_waiter_mutex);
    
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
    
    UVRPC_ATOMIC_ADD(&wg->count, delta);
    return UVRPC_OK;
}

/* Signal one operation done */
int uvrpc_waitgroup_done(uvrpc_waitgroup_t* wg) {
    if (!wg) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    int new_count = UVRPC_ATOMIC_SUB(&wg->count, 1);
    
    /* Note: In a real implementation, we would resolve the completion promise here */
    
    return UVRPC_OK;
}

/* Get count */
int uvrpc_get_count(uvrpc_waitgroup_t* wg) {
    return wg ? UVRPC_ATOMIC_LOAD(&wg->count) : 0;
}

/* Get completion promise (JavaScript-style) */
int uvrpc_waitgroup_get_promise(uvrpc_waitgroup_t* wg, uvrpc_promise_t* promise) {
    if (!wg || !promise) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* In a real implementation, we would track the promise and resolve it when count reaches 0 */
    /* For now, this is a simplified version that always resolves immediately */
    int result = 0;
    return uvrpc_promise_resolve(promise, (uint8_t*)&result, sizeof(int));
}

/* ============================================================================
 * Promise Combinators (JavaScript-style)
 * ============================================================================ */

/* Context for Promise.all() */
typedef struct {
    uvrpc_promise_t* combined;
    int total_count;
    int completed_count;
    volatile int rejected;
    uint8_t** results;
    size_t* result_sizes;
    uv_loop_t* loop;
} promise_all_context_t;

/* Context for Promise.race() */
typedef struct {
    uvrpc_promise_t* combined;
    volatile int completed;
    uv_loop_t* loop;
} promise_race_context_t;

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

/* Callback for Promise.all() individual promise */
static void on_promise_all_callback(uvrpc_promise_t* promise, void* user_data) {
    promise_all_context_t* ctx = (promise_all_context_t*)user_data;
    
    /* If already rejected, do nothing */
    if (UVRPC_ATOMIC_LOAD(&ctx->rejected)) {
        return;
    }
    
    /* Check if this promise rejected */
    if (uvrpc_promise_is_rejected(promise)) {
        /* Mark as rejected and reject combined promise */
        UVRPC_ATOMIC_STORE(&ctx->rejected, 1);
        
        const char* error = uvrpc_promise_get_error(promise);
        int32_t error_code = uvrpc_promise_get_error_code(promise);
        uvrpc_promise_reject(ctx->combined, error_code, error);
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
    int completed = UVRPC_ATOMIC_ADD(&ctx->completed_count, 1);
    
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
        for (int i = 0; i < ctx->total_count; i++) {
            if (ctx->results[i]) {
                UVRPC_FREE(ctx->results[i]);
            }
        }
        UVRPC_FREE(ctx->results);
        UVRPC_FREE(ctx->result_sizes);
        UVRPC_FREE(ctx);
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
    ctx->loop = loop;
    
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
    
    
    
    /* If already completed, do nothing */
    if (UVRPC_ATOMIC_LOAD(&ctx->completed)) {
        return;
    }
    
    /* Mark as completed */
    if (UVRPC_ATOMIC_COMPARE_AND_SWAP(&ctx->completed, 0, 1) == 0) {
        return; /* Another thread already completed */
    }
    
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
    
    
    UVRPC_FREE(ctx);
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
    int completed = UVRPC_ATOMIC_ADD(&ctx->completed_count, 1);
    
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

/* Promise.all() - Synchronous version (blocking) */
int uvrpc_promise_all_sync(uvrpc_promise_t** promises, int count,
                        uint8_t** result, size_t* result_size,
                        uv_loop_t* loop) {
    if (!promises || count <= 0 || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Create combined promise */
    uvrpc_promise_t combined;
    int ret = uvrpc_promise_init(&combined, loop);
    if (ret != UVRPC_OK) {
        return ret;
    }
    
    /* Run Promise.all() */
    ret = uvrpc_promise_all(promises, count, &combined, loop);
    if (ret != UVRPC_OK) {
        uvrpc_promise_cleanup(&combined);
        return ret;
    }
    
    /* Wait for completion */
    ret = uvrpc_promise_wait(&combined);
    
    /* Get result and transfer ownership to caller */
    if (ret == UVRPC_OK && result && result_size) {
        uint8_t* internal_result = NULL;
        size_t internal_size = 0;
        uvrpc_promise_get_result(&combined, &internal_result, &internal_size);
        
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
    
    /* Cleanup - this will free internal result memory */
    uvrpc_promise_cleanup(&combined);
    
    return ret;
}

/* Promise.race() - Synchronous version (blocking) */
int uvrpc_promise_race_sync(uvrpc_promise_t** promises, int count,
                        uint8_t** result, size_t* result_size,
                        uv_loop_t* loop) {
    if (!promises || count <= 0 || !loop) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Create combined promise */
    uvrpc_promise_t combined;
    int ret = uvrpc_promise_init(&combined, loop);
    if (ret != UVRPC_OK) {
        return ret;
    }
    
    /* Run Promise.race() */
    ret = uvrpc_promise_race(promises, count, &combined, loop);
    if (ret != UVRPC_OK) {
        uvrpc_promise_cleanup(&combined);
        return ret;
    }
    
    /* Wait for completion */
    ret = uvrpc_promise_wait(&combined);
    
    /* Get result and transfer ownership to caller */
    if (ret == UVRPC_OK && result && result_size) {
        uint8_t* internal_result = NULL;
        size_t internal_size = 0;
        uvrpc_promise_get_result(&combined, &internal_result, &internal_size);
        
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
    
    /* Cleanup - this will free internal result memory */
    uvrpc_promise_cleanup(&combined);
    
    return ret;
}