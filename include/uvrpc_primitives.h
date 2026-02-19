/**
 * @file uvrpc_primitives.h
 * @brief UVRPC Async Programming Primitives
 * 
 * Provides async programming patterns for concurrent control in UVRPC:
 * - Promise/Future pattern for async operation results
 * - Semaphore for limiting concurrent operations
 * - Barrier for waiting on multiple async operations
 * 
 * Design Principles:
 * - Zero-allocation where possible (stack-allocated structures)
 * - Works with libuv event loop (no blocking calls)
 * - Thread-safe for multi-threaded event loops
 * - Minimal dependencies (only libuv and std C)
 * - Clear error handling
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 * 
 * @section usage Usage Examples
 * 
 * @subsection semaphore Semaphore Example
 * @code
 * // Limit to 10 concurrent RPC calls
 * uvrpc_semaphore_t sem;
 * uvrpc_semaphore_init(&sem, 10);
 * 
 * for (int i = 0; i < 100; i++) {
 *     uvrpc_semaphore_acquire(&sem, loop, on_acquired, &requests[i]);
 * }
 * 
 * // In callback, make RPC call then release
 * void on_acquired(uvrpc_semaphore_t* sem, void* ctx) {
 *     request_t* req = (request_t*)ctx;
 *     uvrpc_client_call(client, req->method, req->params, req->size, on_response, req);
 * }
 * 
 * void on_response(uvrpc_response_t* resp, void* ctx) {
 *     request_t* req = (request_t*)ctx;
 *     // Process response
 *     uvrpc_semaphore_release(&sem);
 * }
 * @endcode
 * 
 * @subsection barrier Barrier Example
 * @code
 * // Wait for 5 RPC calls to complete
 * uvrpc_barrier_t barrier;
 * uvrpc_barrier_init(&barrier, loop, 5, on_all_done, user_data);
 * 
 * for (int i = 0; i < 5; i++) {
 *     uvrpc_client_call(client, methods[i], params[i], sizes[i], on_barrier_callback, &barrier);
 * }
 * 
 * void on_barrier_callback(uvrpc_response_t* resp, void* ctx) {
 *     uvrpc_barrier_t* barrier = (uvrpc_barrier_t*)ctx;
 *     // Process individual response
 *     uvrpc_barrier_wait(barrier, resp, NULL);
 * }
 * 
 * void on_all_done(uvrpc_barrier_t* barrier, void* user_data) {
 *     // All 5 calls completed
 *     int total_errors = uvrpc_barrier_get_error_count(barrier);
 *     // Process results
 * }
 * @endcode
 * 
 * @subsection promise Promise Example
 * @code
 * // Chain async operations
 * uvrpc_promise_t promise;
 * uvrpc_promise_init(&promise, loop);
 * 
 * // Step 1: Get user
 * uvrpc_client_call(client, "getUser", user_id, sizeof(user_id), on_user, &promise);
 * 
 * void on_user(uvrpc_response_t* resp, void* ctx) {
 *     uvrpc_promise_t* promise = (uvrpc_promise_t*)ctx;
 *     
 *     if (resp->error_code != 0) {
 *         uvrpc_promise_reject(promise, resp->error_code, "Failed to get user");
 *         return;
 *     }
 *     
 *     // Step 2: Get user orders (chain)
 *     uvrpc_promise_set_callback(promise, on_orders, promise);
 *     uvrpc_client_call(client, "getOrders", resp->result, resp->result_size, on_orders, promise);
 * }
 * 
 * void on_orders(uvrpc_response_t* resp, void* ctx) {
 *     uvrpc_promise_t* promise = (uvrpc_promise_t*)ctx;
 *     
 *     if (resp->error_code != 0) {
 *         uvrpc_promise_reject(promise, resp->error_code, "Failed to get orders");
 *         return;
 *     }
 *     
 *     // Resolve promise with final result
 *     uvrpc_promise_resolve(promise, resp->result, resp->result_size);
 * }
 * 
 * // Final callback when chain completes
 * void on_final(uvrpc_promise_t* promise, void* user_data) {
 *     if (uvrpc_promise_is_fulfilled(promise)) {
 *         uint8_t* result;
 *         size_t size;
 *         uvrpc_promise_get_result(promise, &result, &size);
 *         // Use result
 *     } else {
 *         const char* error = uvrpc_promise_get_error(promise);
 *         // Handle error
 *     }
 * }
 * 
 * uvrpc_promise_then(&promise, on_final, user_data);
 * @endcode
 */

#ifndef UVRPC_PRIMITIVES_H
#define UVRPC_PRIMITIVES_H

#include "uvrpc.h"
#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct uvrpc_promise uvrpc_promise_t;
typedef struct uvrpc_semaphore uvrpc_semaphore_t;
typedef struct uvrpc_barrier uvrpc_barrier_t;
typedef struct uvrpc_waitgroup uvrpc_waitgroup_t;

/* ============================================================================
 * Promise/Future Pattern
 * ============================================================================ */

/**
 * @brief Promise state enumeration
 */
typedef enum {
    UVRPC_PROMISE_PENDING = 0,    /**< @brief Promise is pending */
    UVRPC_PROMISE_FULFILLED = 1,  /**< @brief Promise resolved successfully */
    UVRPC_PROMISE_REJECTED = 2    /**< @brief Promise rejected with error */
} uvrpc_promise_state_t;

/**
 * @brief Promise result callback type
 *
 * Called when the promise is fulfilled or rejected.
 *
 * @param promise Promise instance
 * @param user_data User data provided in uvrpc_promise_then()
 */
typedef void (*uvrpc_promise_callback_t)(uvrpc_promise_t* promise, void* user_data);

/**
 * @brief Promise structure
 * 
 * Represents an async operation that will complete in the future.
 * Can be fulfilled (success) or rejected (error).
 * 
 * @note This structure should be allocated on stack or via uvrpc_alloc().
 *       Do not free it while callbacks may still be pending.
 */
typedef struct uvrpc_promise {
    uv_loop_t* loop;                          /**< @brief libuv event loop */
    uvrpc_promise_state_t state;              /**< @brief Current state */
    uint8_t* result;                          /**< @brief Result data (if fulfilled) */
    size_t result_size;                       /**< @brief Result size */
    char* error_message;                      /**< @brief Error message (if rejected) */
    int32_t error_code;                       /**< @brief Error code (if rejected) */
    uvrpc_promise_callback_t callback;        /**< @brief Completion callback */
    void* callback_data;                      /**< @brief User data for callback */
    uv_async_t async_handle;                  /**< @brief Async handle for callback */
    int is_callback_scheduled;                /**< @brief Whether callback is scheduled */
} uvrpc_promise_t;

/**
 * @defgroup PromiseAPI Promise API
 * @brief Functions for Promise/Future pattern
 * @{
 */

/**
 * @brief Initialize a promise
 * 
 * @param promise Promise structure to initialize
 * @param loop libuv event loop
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_init(uvrpc_promise_t* promise, uv_loop_t* loop);

/**
 * @brief Cleanup a promise
 * 
 * Frees any allocated resources. Does not free the promise structure itself.
 * 
 * @param promise Promise to cleanup
 */
void uvrpc_promise_cleanup(uvrpc_promise_t* promise);

/**
 * @brief Resolve a promise with a result
 * 
 * @param promise Promise to resolve
 * @param result Result data
 * @param result_size Size of result data
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_resolve(uvrpc_promise_t* promise, const uint8_t* result, size_t result_size);

/**
 * @brief Reject a promise with an error
 * 
 * @param promise Promise to reject
 * @param error_code Error code
 * @param error_message Error message
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_reject(uvrpc_promise_t* promise, int32_t error_code, const char* error_message);

/**
 * @brief Set callback for promise completion
 *
 * The callback will be called when the promise is fulfilled or rejected.
 *
 * @param promise Promise instance
 * @param callback Completion callback
 * @param user_data User data passed to callback
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_then(uvrpc_promise_t* promise, uvrpc_promise_callback_t callback, void* user_data);

/**
 * @brief Set intermediate callback for promise chaining
 *
 * This is an alias for uvrpc_promise_then() used for internal chaining.
 *
 * @param promise Promise instance
 * @param callback Completion callback
 * @param user_data User data passed to callback
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_set_callback(uvrpc_promise_t* promise, uvrpc_promise_callback_t callback, void* user_data);

/**
 * @brief Check if promise is fulfilled
 *
 * @param promise Promise instance
 * @return 1 if fulfilled, 0 otherwise
 */
int uvrpc_promise_is_fulfilled(uvrpc_promise_t* promise);

/**
 * @brief Check if promise is rejected
 * 
 * @param promise Promise instance
 * @return 1 if rejected, 0 otherwise
 */
int uvrpc_promise_is_rejected(uvrpc_promise_t* promise);

/**
 * @brief Check if promise is pending
 * 
 * @param promise Promise instance
 * @return 1 if pending, 0 otherwise
 */
int uvrpc_promise_is_pending(uvrpc_promise_t* promise);

/**
 * @brief Get result from fulfilled promise
 * 
 * @param promise Promise instance
 * @param result Output pointer to result data
 * @param result_size Output pointer to result size
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_get_result(uvrpc_promise_t* promise, uint8_t** result, size_t* result_size);

/**
 * @brief Get error from rejected promise
 * 
 * @param promise Promise instance
 * @return Error message string, or NULL if not rejected
 */
const char* uvrpc_promise_get_error(uvrpc_promise_t* promise);

/**
 * @brief Get error code from rejected promise
 * 
 * @param promise Promise instance
 * @return Error code, or 0 if not rejected
 */
int32_t uvrpc_promise_get_error_code(uvrpc_promise_t* promise);

/** @} */

/* ============================================================================
 * Semaphore Pattern (JavaScript-style)
 * ============================================================================ */

/**
 * @brief Semaphore structure
 * 
 * Controls concurrent operations by limiting the number of permits available.
 * 
 * JavaScript-style usage:
 * @code
 * uvrpc_semaphore_t sem;
 * uvrpc_semaphore_init(&sem, &loop, 3);  // Max 3 concurrent operations
 * 
 * uvrpc_promise_t* p = malloc(sizeof(uvrpc_promise_t));
 * uvrpc_promise_init(p, &loop);
 * uvrpc_semaphore_acquire_async(&sem, p);
 * 
 * uvrpc_promise_then(p, [](auto* promise, auto* ctx) {
 *     // Permit acquired, do work
 *     uvrpc_semaphore_release(&sem);
 *     uvrpc_promise_cleanup(p);
 *     free(p);
 * }, NULL);
 * @endcode
 * 
 * @note Thread-safe using atomic operations.
 */
typedef struct uvrpc_semaphore {
    uv_loop_t* loop;                          /**< @brief libuv event loop */
    volatile int permits;                     /**< @brief Available permits (atomic) */
    volatile int waiting;                     /**< @brief Number of waiting operations (atomic) */
    uv_async_t async_handle;                  /**< @brief Async handle for notifying waiters */
} uvrpc_semaphore_t;

/**
 * @defgroup SemaphoreAPI Semaphore API
 * @brief Functions for Semaphore pattern
 * @{
 */

/**
 * @brief Initialize a semaphore
 * 
 * @param semaphore Semaphore structure to initialize
 * @param loop libuv event loop
 * @param permits Initial number of permits
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_semaphore_init(uvrpc_semaphore_t* semaphore, uv_loop_t* loop, int permits);

/**
 * @brief Cleanup a semaphore
 * 
 * @param semaphore Semaphore to cleanup
 */
void uvrpc_semaphore_cleanup(uvrpc_semaphore_t* semaphore);

/**
 * @brief Acquire a semaphore permit asynchronously (JavaScript-style)
 * 
 * Returns a Promise that resolves when a permit is acquired.
 * 
 * JavaScript-style usage:
 * @code
 * uvrpc_promise_t* promise = malloc(sizeof(uvrpc_promise_t));
 * uvrpc_promise_init(promise, &loop);
 * 
 * if (uvrpc_semaphore_acquire_async(&sem, promise) == UVRPC_OK) {
 *     uvrpc_promise_then(promise, on_permit_acquired, context);
 * }
 * @endcode
 * 
 * @param semaphore Semaphore instance
 * @param promise Promise to resolve when permit is acquired
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_semaphore_acquire_async(uvrpc_semaphore_t* semaphore, 
                                    uvrpc_promise_t* promise);

/**
 * @brief Release a semaphore permit
 * 
 * Wakes up one waiting operation if any.
 * 
 * @param semaphore Semaphore instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_semaphore_release(uvrpc_semaphore_t* semaphore);

/**
 * @brief Try to acquire a permit (immediate, non-blocking)
 * 
 * @param semaphore Semaphore instance
 * @return 1 if permit acquired, 0 if no permit available
 */
int uvrpc_semaphore_try_acquire(uvrpc_semaphore_t* semaphore);

/**
 * @brief Get available permit count
 * 
 * @param semaphore Semaphore instance
 * @return Number of available permits
 */
int uvrpc_semaphore_get_available(uvrpc_semaphore_t* semaphore);

/**
 * @brief Get waiting operation count
 * 
 * @param semaphore Semaphore instance
 * @return Number of waiting operations
 */
int uvrpc_semaphore_get_waiting_count(uvrpc_semaphore_t* semaphore);

/** @} */

/* ============================================================================
 * Wait Group Pattern (JavaScript-style)
 * ============================================================================ */

/**
 * @brief WaitGroup structure
 *
 * JavaScript-style usage:
 * @code
 * uvrpc_waitgroup_t wg;
 * uvrpc_waitgroup_init(&wg, &loop);
 * uvrpc_waitgroup_add(&wg, 10);
 * 
 * // In each async operation callback:
 * uvrpc_waitgroup_done(&wg);
 * 
 * // Get completion promise
 * uvrpc_promise_t* p = malloc(sizeof(uvrpc_promise_t));
 * uvrpc_promise_init(p, &loop);
 * uvrpc_waitgroup_get_promise(&wg, p);
 * 
 * uvrpc_promise_then(p, on_all_done, context);
 * @endcode
 */
typedef struct uvrpc_waitgroup {
    uv_loop_t* loop;                          /**< @brief libuv event loop */
    volatile int count;                       /**< @brief Operation count (atomic) */
    uv_async_t async_handle;                  /**< @brief Async handle for notification */
    int is_callback_scheduled;                /**< @brief Whether callback is scheduled */
} uvrpc_waitgroup_t;

/**
 * @defgroup WaitGroupAPI WaitGroup API
 * @brief Functions for WaitGroup pattern
 * @{
 */

/**
 * @brief Initialize a wait group
 *
 * @param wg WaitGroup structure to initialize
 * @param loop libuv event loop
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_waitgroup_init(uvrpc_waitgroup_t* wg, uv_loop_t* loop);

/**
 * @brief Cleanup a wait group
 * 
 * @param wg WaitGroup to cleanup
 */
void uvrpc_waitgroup_cleanup(uvrpc_waitgroup_t* wg);

/**
 * @brief Add operations to wait for
 * 
 * @param wg WaitGroup instance
 * @param delta Number of operations to add (can be negative)
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_waitgroup_add(uvrpc_waitgroup_t* wg, int delta);

/**
 * @brief Signal one operation complete
 * 
 * Decrements the count. When count reaches 0, the completion promise is resolved.
 * 
 * @param wg WaitGroup instance
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_waitgroup_done(uvrpc_waitgroup_t* wg);

/**
 * @brief Get current count
 * 
 * @param wg WaitGroup instance
 * @return Current operation count
 */
int uvrpc_get_count(uvrpc_waitgroup_t* wg);

/**
 * @brief Get completion promise (JavaScript-style)
 * 
 * Returns a Promise that resolves when all operations are complete.
 * 
 * JavaScript-style usage:
 * @code
 * uvrpc_promise_t* p = uvrpc_waitgroup_get_promise(&wg);
 * uvrpc_promise_then(p, on_all_complete, context);
 * @endcode
 * 
 * @param wg WaitGroup instance
 * @param promise Promise to resolve when count reaches 0
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_waitgroup_get_promise(uvrpc_waitgroup_t* wg, uvrpc_promise_t* promise);

/** @} */

/* ============================================================================
 * Promise Combinators (JavaScript-style)
 * ============================================================================ */

/**
 * @defgroup PromiseCombinators Promise Combinators
 * @brief JavaScript-style Promise combinators (all, race, allSettled)
 * @{
 */

/**
 * @brief Promise.all() - Wait for all promises to fulfill
 * 
 * Similar to JavaScript's Promise.all(). Waits for all promises to fulfill.
 * If any promise rejects, the combined promise rejects immediately.
 * 
 * @param promises Array of promises
 * @param count Number of promises
 * @param combined Promise to hold combined result
 * @param loop libuv event loop
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_all(
    uvrpc_promise_t** promises,
    int count,
    uvrpc_promise_t* combined,
    uv_loop_t* loop);

/**
 * @brief Promise.race() - Wait for first promise to complete
 * 
 * Similar to JavaScript's Promise.race(). Resolves or rejects when the first
 * promise completes (fulfills or rejects).
 * 
 * @param promises Array of promises
 * @param count Number of promises
 * @param combined Promise to hold first result
 * @param loop libuv event loop
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_race(
    uvrpc_promise_t** promises,
    int count,
    uvrpc_promise_t* combined,
    uv_loop_t* loop);

/**
 * @brief Promise.allSettled() - Wait for all promises to complete
 * 
 * Similar to JavaScript's Promise.allSettled(). Waits for all promises to complete
 * (fulfill or reject), collecting all results and errors.
 * 
 * @param promises Array of promises
 * @param count Number of promises
 * @param combined Promise to hold settled results
 * @param loop libuv event loop
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_promise_all_settled(
    uvrpc_promise_t** promises,
    int count,
    uvrpc_promise_t* combined,
    uv_loop_t* loop);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_PRIMITIVES_H */