/**
 * @file uvasync.h
 * @brief UVRPC Async Concurrency Control Abstraction
 * 
 * Provides a unified abstraction layer for libuv-based async concurrency control.
 * Simplifies the use of async primitives (Promise, Semaphore, WaitGroup) through
 * a high-level scheduler interface.
 * 
 * Design Principles:
 * - Zero-abstraction overhead on critical paths
 * - Stack allocation preferred for structures
 * - Works seamlessly with libuv event loops
 * - Thread-safe for multi-threaded event loops
 * - Minimal dependencies (only libuv and std C)
 * 
 * @author UVRPC Team
 * @date 2026
 * @version 1.0
 * 
 * @copyright Copyright (c) 2026
 * @license MIT License
 * 
 * @section overview Overview
 * 
 * uvasync provides a high-level abstraction for managing concurrent async operations:
 * 
 * 1. **Async Context**: Encapsulates event loop and async state
 * 2. **Async Scheduler**: Manages task scheduling and concurrency limits
 * 3. **Task Queue**: FIFO queue for pending tasks
 * 4. **Concurrency Control**: Automatic semaphore-based limiting
 * 
 * @section usage Usage Examples
 * 
 * @subsection basic Basic Usage
 * @code
 * // Create context with new loop
 * uvasync_context_t* ctx = uvasync_context_create_new();
 * 
 * // Create scheduler with max 10 concurrent tasks
 * uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 10);
 * 
 * // Submit task
 * uvrpc_promise_t* promise = uvrpc_promise_create(ctx->loop);
 * uvrpc_promise_then(promise, on_complete, NULL);
 * uvasync_submit(scheduler, my_task_fn, task_data, promise);
 * 
 * // Wait for all tasks to complete
 * uvasync_scheduler_wait_all(scheduler, 5000);
 * 
 * // Cleanup
 * uvasync_scheduler_destroy(scheduler);
 * uvasync_context_destroy(ctx);
 * @endcode
 * 
 * @subsection batch Batch Processing
 * @code
 * uvasync_task_t tasks[100];
 * uvrpc_promise_t* promises[100];
 * 
 * // Prepare tasks
 * for (int i = 0; i < 100; i++) {
 *     tasks[i].fn = process_item;
 *     tasks[i].data = &items[i];
 *     promises[i] = uvrpc_promise_create(ctx->loop);
 * }
 * 
 * // Submit batch
 * uvasync_submit_batch(scheduler, tasks, 100, promises);
 * 
 * // Wait all complete
 * uvasync_scheduler_wait_all(scheduler, 0);  // No timeout
 * @endcode
 * 
 * @subsection dynamic Dynamic Concurrency
 * @code
 * // Start with 5 concurrent tasks
 * uvasync_scheduler_t* scheduler = uvasync_scheduler_create(ctx, 5);
 * 
 * // Increase when system load is low
 * if (system_load < 0.3) {
 *     uvasync_scheduler_set_concurrency(scheduler, 20);
 * }
 * 
 * // Decrease when system load is high
 * if (system_load > 0.8) {
 *     uvasync_scheduler_set_concurrency(scheduler, 3);
 * }
 * @endcode
 */

#ifndef UVASYNC_H
#define UVASYNC_H

#include "uvrpc.h"
#include "uvrpc_primitives.h"
#include <uv.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Codes
 * ============================================================================ */

/**
 * @brief uvasync error codes
 * 
 * All uvasync error codes are negative to avoid conflicts with UVRPC codes.
 * Positive values are reserved for future use.
 */
#define UVASYNC_OK                       0   /**< @brief Success */
#define UVASYNC_ERROR                   -1   /**< @brief Generic error */

/* Context errors (-100 to -199) */
#define UVASYNC_ERROR_CONTEXT_INVALID        -100  /**< @brief Invalid context */
#define UVASYNC_ERROR_CONTEXT_NO_LOOP        -101  /**< @brief Context has no event loop */
#define UVASYNC_ERROR_CONTEXT_LOOP_INIT_FAILED -102 /**< @brief Failed to initialize event loop */

/* Scheduler errors (-200 to -299) */
#define UVASYNC_ERROR_SCHEDULER_INVALID       -200  /**< @brief Invalid scheduler */
#define UVASYNC_ERROR_SCHEDULER_INIT_FAILED   -201  /**< @brief Failed to initialize scheduler */
#define UVASYNC_ERROR_SCHEDULER_CONCURRENCY_INVALID -202 /**< @brief Invalid concurrency limit */

/* Task errors (-300 to -399) */
#define UVASYNC_ERROR_TASK_INVALID            -300  /**< @brief Invalid task */
#define UVASYNC_ERROR_TASK_SUBMIT_FAILED      -301  /**< @brief Failed to submit task */
#define UVASYNC_ERROR_TASK_CANCELLED          -302  /**< @brief Task was cancelled */

/* Wait errors (-400 to -499) */
#define UVASYNC_ERROR_WAIT_TIMEOUT            -400  /**< @brief Wait operation timed out */
#define UVASYNC_ERROR_WAIT_INVALID            -401  /**< @brief Invalid wait operation */

/* Memory errors (-500 to -599) */
#define UVASYNC_ERROR_NO_MEMORY               -500  /**< @brief Out of memory */

/* Parameter errors (-600 to -699) */
#define UVASYNC_ERROR_INVALID_PARAM           -600  /**< @brief Invalid parameter */

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct uvasync_context uvasync_context_t;
typedef struct uvasync_scheduler uvasync_scheduler_t;
typedef struct uvasync_task uvasync_task_t;
typedef struct uvasync_stats uvasync_stats_t;

/* ============================================================================
 * Async Context
 * ============================================================================ */

/**
 * @brief Async context - encapsulates event loop and async state
 * 
 * The context holds the libuv event loop and provides a unified interface
 * for async operations. Can either own its own loop or use an existing one.
 * 
 * @note Zero-allocation when stack-allocated (~50 bytes)
 */
struct uvasync_context {
    uv_loop_t* loop;               /**< @brief libuv event loop */
    int owns_loop;                 /**< @brief Whether to destroy loop on cleanup */
    void* user_data;               /**< @brief User data for callbacks */
};

/**
 * @defgroup ContextAPI Context API
 * @brief Functions for async context management
 * @{
 */

/**
 * @brief Create async context with new event loop
 * 
 * Creates a new libuv event loop and wraps it in a context.
 * The context will own the loop and destroy it on cleanup.
 * 
 * @return Pointer to context, or NULL on failure
 */
uvasync_context_t* uvasync_context_create_new(void);

/**
 * @brief Create async context with existing event loop
 * 
 * Wraps an existing libuv event loop in a context.
 * The context will NOT own the loop (caller responsible for cleanup).
 * 
 * @param loop Existing libuv event loop
 * @return Pointer to context, or NULL on failure
 */
uvasync_context_t* uvasync_context_create(uv_loop_t* loop);

/**
 * @brief Destroy async context
 * 
 * If the context owns the loop (created with create_new), it will be
 * destroyed. Otherwise, only the context structure is freed.
 * 
 * @param ctx Context to destroy
 */
void uvasync_context_destroy(uvasync_context_t* ctx);

/**
 * @brief Set user data for context
 * 
 * @param ctx Context instance
 * @param user_data User data pointer
 */
void uvasync_context_set_user_data(uvasync_context_t* ctx, void* user_data);

/**
 * @brief Get user data from context
 * 
 * @param ctx Context instance
 * @return User data pointer, or NULL if not set
 */
void* uvasync_context_get_user_data(uvasync_context_t* ctx);

/** @} */

/* ============================================================================
 * Async Task
 * ============================================================================ */

/**
 * @brief Task function type
 * 
 * @param data User data passed to task
 * @param promise Promise to resolve/reject with result
 * @return void
 */
typedef void (*uvasync_task_fn_t)(void* data, uvrpc_promise_t* promise);

/**
 * @brief Async task - represents a unit of async work
 * 
 * Tasks are submitted to the scheduler and executed with concurrency limits.
 * Results are communicated through Promises.
 */
struct uvasync_task {
    uvasync_task_fn_t fn;        /**< @brief Task function */
    void* data;                  /**< @brief Task data */
    uvrpc_promise_t* promise;    /**< @brief Result promise */
    void* user_data;             /**< @brief User data for task */
};

/* ============================================================================
 * Async Scheduler
 * ============================================================================ */

/**
 * @brief Async scheduler - manages task execution and concurrency
 * 
 * The scheduler provides:
 * - Automatic concurrency limiting (using Semaphore)
 * - Task queue management (FIFO)
 * - Statistics tracking
 * - Dynamic concurrency adjustment
 * 
 * @note Zero-allocation when stack-allocated (~200 bytes)
 */
struct uvasync_scheduler {
    uvasync_context_t* ctx;              /**< @brief Async context */
    uvrpc_semaphore_t concurrency_limit; /**< @brief Concurrency limit semaphore */
    uvrpc_waitgroup_t waitgroup;         /**< @brief Wait for all tasks */
    volatile int active_tasks;            /**< @brief Currently running tasks (atomic) */
    volatile int submitted_tasks;         /**< @brief Total tasks submitted (atomic) */
    volatile int completed_tasks;         /**< @brief Total tasks completed (atomic) */
    volatile int failed_tasks;            /**< @brief Total tasks failed (atomic) */
    uvasync_stats_t* stats;              /**< @brief Detailed statistics (pointer to avoid incomplete type) */
};

/**
 * @brief Scheduler statistics
 */
struct uvasync_stats {
    uint64_t total_submitted;      /**< @brief Total tasks submitted */
    uint64_t total_completed;      /**< @brief Total tasks completed */
    uint64_t total_failed;         /**< @brief Total tasks failed */
    uint64_t total_cancelled;      /**< @brief Total tasks cancelled */
    uint64_t peak_concurrency;     /**< @brief Peak concurrent tasks */
    double avg_task_duration_ms;   /**< @brief Average task duration (ms) */
    uint64_t total_wait_time_ms;   /**< @brief Total wait time in queue (ms) */
};

/**
 * @defgroup SchedulerAPI Scheduler API
 * @brief Functions for async scheduler management
 * @{
 */

/**
 * @brief Create async scheduler
 * 
 * Creates a scheduler with a maximum concurrency limit.
 * The scheduler uses a Semaphore internally to enforce the limit.
 * 
 * @param ctx Async context
 * @param max_concurrency Maximum concurrent tasks (0 = unlimited)
 * @return Pointer to scheduler, or NULL on failure
 */
uvasync_scheduler_t* uvasync_scheduler_create(
    uvasync_context_t* ctx,
    int max_concurrency
);

/**
 * @brief Destroy async scheduler
 * 
 * Waits for all pending tasks to complete, then cleans up resources.
 * 
 * @param scheduler Scheduler to destroy
 */
void uvasync_scheduler_destroy(uvasync_scheduler_t* scheduler);

/**
 * @brief Submit a single task to scheduler
 * 
 * The task will be queued and executed when a concurrency slot is available.
 * Results are communicated through the provided Promise.
 * 
 * @code
 * uvrpc_promise_t* promise = uvrpc_promise_create(ctx->loop);
 * uvrpc_promise_then(promise, on_complete, NULL);
 *
 * uvasync_submit(scheduler, my_task_fn, task_data, promise);
 * @endcode
 *
 * @param scheduler Scheduler instance
 * @param fn Task function
 * @param data Task data
 * @param promise Promise for task result
 * @return UVASYNC_OK on success, uvasync error code on failure
 */
int uvasync_submit(
    uvasync_scheduler_t* scheduler,
    uvasync_task_fn_t fn,
    void* data,
    uvrpc_promise_t* promise
);

/**
 * @brief Submit batch of tasks to scheduler
 * 
 * Submits multiple tasks at once for better performance.
 * Results are communicated through the provided Promises.
 * 
 * @code
 * uvasync_task_t tasks[100];
 * uvrpc_promise_t* promises[100];
 *
 * // Prepare tasks...
 *
 * uvasync_submit_batch(scheduler, tasks, 100, promises);
 * @endcode
 *
 * @param scheduler Scheduler instance
 * @param tasks Array of tasks
 * @param count Number of tasks
 * @param promises Array of promises (must have count elements)
 * @return UVASYNC_OK on success, uvasync error code on failure
 */
int uvasync_submit_batch(
    uvasync_scheduler_t* scheduler,
    uvasync_task_t* tasks,
    int count,
    uvrpc_promise_t** promises
);

/**
 * @brief Set concurrency limit dynamically
 * 
 * Adjusts the maximum number of concurrent tasks.
 * Can be called at any time to adapt to system load.
 *
 * @param scheduler Scheduler instance
 * @param max_concurrency New maximum (0 = unlimited)
 * @return UVASYNC_OK on success, uvasync error code on failure
 */
int uvasync_scheduler_set_concurrency(
    uvasync_scheduler_t* scheduler,
    int max_concurrency
);

/**
 * @brief Get current concurrency limit
 * 
 * @param scheduler Scheduler instance
 * @return Maximum concurrent tasks (0 = unlimited)
 */
int uvasync_scheduler_get_concurrency_limit(uvasync_scheduler_t* scheduler);

/**
 * @brief Get current active task count
 * 
 * @param scheduler Scheduler instance
 * @return Number of currently running tasks
 */
int uvasync_scheduler_get_active_count(uvasync_scheduler_t* scheduler);

/**
 * @brief Get pending task count
 * 
 * @param scheduler Scheduler instance
 * @return Number of tasks waiting for concurrency slot
 */
int uvasync_scheduler_get_pending_count(uvasync_scheduler_t* scheduler);

/**
 * @brief Wait for all tasks to complete
 * 
 * Blocks until all submitted tasks have completed or timeout expires.
 *
 * @param scheduler Scheduler instance
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return UVASYNC_OK if all completed, UVASYNC_ERROR_WAIT_TIMEOUT if timeout
 */
int uvasync_scheduler_wait_all(
    uvasync_scheduler_t* scheduler,
    uint64_t timeout_ms
);

/**
 * @brief Get scheduler statistics
 * 
 * @param scheduler Scheduler instance
 * @return Pointer to statistics structure (valid until scheduler destroyed)
 */
const uvasync_stats_t* uvasync_scheduler_get_stats(
    uvasync_scheduler_t* scheduler
);

/**
 * @brief Reset scheduler statistics
 * 
 * Clears all accumulated statistics counters.
 * 
 * @param scheduler Scheduler instance
 */
void uvasync_scheduler_reset_stats(uvasync_scheduler_t* scheduler);

/** @} */

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * @defgroup ConvenienceAPI Convenience Functions
 * @brief High-level helper functions
 * @{
 */

/**
 * @brief Submit task and wait for result (blocking)
 * 
 * Convenience function that submits a task and blocks until it completes.
 * WARNING: This blocks the current thread, do not use in event loop callbacks.
 * 
 * @code
 * uint8_t* result = NULL;
 * size_t result_size = 0;
 * int ret = uvasync_submit_and_wait(scheduler, my_task_fn, task_data,
 *                                      &result, &result_size, 5000);
 * if (ret == UVASYNC_OK) {
 *     // Use result
 *     uvrpc_free(result);
 * }
 * @endcode
 *
 * @param scheduler Scheduler instance
 * @param fn Task function
 * @param data Task data
 * @param result Output pointer to result data (caller must free)
 * @param result_size Output pointer to result size
 * @param timeout_ms Timeout in milliseconds
 * @return UVASYNC_OK on success, uvasync error code on failure
 */
int uvasync_submit_and_wait(
    uvasync_scheduler_t* scheduler,
    uvasync_task_fn_t fn,
    void* data,
    uint8_t** result,
    size_t* result_size,
    uint64_t timeout_ms
);

/**
 * @brief Submit batch of tasks and wait for all to complete (blocking)
 * 
 * Convenience function that submits a batch of tasks and blocks until
 * all complete. Returns array of results.
 * 
 * @param scheduler Scheduler instance
 * @param tasks Array of tasks
 * @param count Number of tasks
 * @param results Output array of result pointers (caller must free each)
 * @param result_sizes Output array of result sizes
 * @param timeout_ms Timeout in milliseconds
 * @return UVASYNC_OK on success, uvasync error code on failure
 */
int uvasync_submit_batch_and_wait(
    uvasync_scheduler_t* scheduler,
    uvasync_task_t* tasks,
    int count,
    uint8_t*** results,
    size_t** result_sizes,
    uint64_t timeout_ms
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* UVASYNC_H */