/**
 * UVRPC Async/Await API
 * Provides coroutine-like async/await syntax for C
 * 
 * Usage:
 *   UVRPC_ASYNC(client, {
 *       UVRPC_AWAIT(uvrpc_client_call_async(client, "echo", data, size, timeout_ms));
 *       if (result->error_code != 0) {
 *           // Handle error
 *       }
 *       // Use result->result and result->result_size
 *   });
 */

#ifndef UVRPC_ASYNC_H
#define UVRPC_ASYNC_H

#include "uvrpc.h"
#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Async context for coroutine */
typedef struct uvrpc_async_ctx uvrpc_async_ctx_t;

/* Async result structure */
typedef struct uvrpc_async_result {
    int status;              /* UVRPC_OK or error code */
    uint32_t msgid;          /* Message ID */
    int32_t error_code;      /* Error code from response */
    uint8_t* result;         /* Response data */
    size_t result_size;      /* Response data size */
    void* user_data;         /* User data */
} uvrpc_async_result_t;

/**
 * Create async context
 * @param loop libuv event loop
 * @return async context
 */
uvrpc_async_ctx_t* uvrpc_async_ctx_new(uv_loop_t* loop);

/**
 * Free async context
 * @param ctx async context
 */
void uvrpc_async_ctx_free(uvrpc_async_ctx_t* ctx);

/**
 * Execute async block
 * @param ctx async context
 * @param timeout_ms timeout in milliseconds (0 for no timeout)
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_async_exec(uvrpc_async_ctx_t* ctx, uint64_t timeout_ms);

/**
 * Client call with async/await
 * This function should be called within UVRPC_ASYNC block
 * @param client RPC client
 * @param method method name
 * @param params request parameters
 * @param params_size parameter size
 * @param result output result pointer
 * @return UVRPC_OK on success
 */
int uvrpc_client_call_async(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                             const char* method, const uint8_t* params,
                             size_t params_size, uvrpc_async_result_t** result);

/**
 * Free async result
 * @param result result to free
 */
void uvrpc_async_result_free(uvrpc_async_result_t* result);

/**
 * Execute multiple async calls concurrently and wait for all to complete (Promise.all)
 * @param ctx async context
 * @param clients array of RPC clients
 * @param methods array of method names
 * @param params_array array of parameters
 * @param params_sizes array of parameter sizes
 * @param results output array of results (caller must free each result)
 * @param count number of calls
 * @param timeout_ms timeout in milliseconds (0 for no timeout)
 * @return UVRPC_OK on success, UVRPC_ERROR_TIMEOUT on timeout
 */
int uvrpc_async_all(uvrpc_async_ctx_t* ctx,
                     uvrpc_client_t** clients,
                     const char** methods,
                     const uint8_t** params_array,
                     size_t* params_sizes,
                     uvrpc_async_result_t*** results,
                     int count,
                     uint64_t timeout_ms);

/**
 * Execute multiple async calls concurrently and wait for any to complete (Promise.any)
 * @param ctx async context
 * @param clients array of RPC clients
 * @param methods array of method names
 * @param params_array array of parameters
 * @param params_sizes array of parameter sizes
 * @param result output result (caller must free)
 * @param completed_index index of the completed call
 * @param count number of calls
 * @param timeout_ms timeout in milliseconds (0 for no timeout)
 * @return UVRPC_OK on success, UVRPC_ERROR_TIMEOUT on timeout
 */
int uvrpc_async_any(uvrpc_async_ctx_t* ctx,
                     uvrpc_client_t** clients,
                     const char** methods,
                     const uint8_t** params_array,
                     size_t* params_sizes,
                     uvrpc_async_result_t** result,
                     int* completed_index,
                     int count,
                     uint64_t timeout_ms);

/**
 * Execute multiple async calls concurrently and wait for race (first to complete wins, similar to Promise.any)
 * @param ctx async context
 * @param clients array of RPC clients
 * @param methods array of method names
 * @param params_array array of parameters
 * @param params_sizes array of parameter sizes
 * @param result output result (caller must free)
 * @param completed_index index of the completed call
 * @param count number of calls
 * @param timeout_ms timeout in milliseconds (0 for no timeout)
 * @return UVRPC_OK on success, UVRPC_ERROR_TIMEOUT on timeout
 */
int uvrpc_async_race(uvrpc_async_ctx_t* ctx,
                      uvrpc_client_t** clients,
                      const char** methods,
                      const uint8_t** params_array,
                      size_t* params_sizes,
                      uvrpc_async_result_t** result,
                      int* completed_index,
                      int count,
                      uint64_t timeout_ms);

/**
 * Execute async call with retry and exponential backoff (Promise.retry)
 * @param ctx async context
 * @param client RPC client
 * @param method method name
 * @param params request parameters
 * @param params_size parameter size
 * @param result output result pointer
 * @param max_retries maximum number of retries
 * @param initial_delay_ms initial delay between retries in milliseconds
 * @param backoff_multiplier multiplier for exponential backoff (e.g., 2.0)
 * @return UVRPC_OK on success, error code on failure
 */
int uvrpc_async_retry_with_backoff(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                                   const char* method, const uint8_t* params,
                                   size_t params_size, uvrpc_async_result_t** result,
                                   int max_retries, uint64_t initial_delay_ms,
                                   double backoff_multiplier);

/**
 * Execute async call with timeout (Promise.timeout)
 * @param ctx async context
 * @param client RPC client
 * @param method method name
 * @param params request parameters
 * @param params_size parameter size
 * @param result output result pointer
 * @param timeout_ms timeout in milliseconds
 * @return UVRPC_OK on success, UVRPC_ERROR_TIMEOUT on timeout
 */
int uvrpc_async_timeout(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                         const char* method, const uint8_t* params,
                         size_t params_size, uvrpc_async_result_t** result,
                         uint64_t timeout_ms);

/**
 * Cancel pending async operations
 * @param ctx async context
 * @return UVRPC_OK on success
 */
int uvrpc_async_cancel_all(uvrpc_async_ctx_t* ctx);

/**
 * Get number of pending async operations
 * @param ctx async context
 * @return number of pending operations
 */
int uvrpc_async_get_pending_count(uvrpc_async_ctx_t* ctx);

/* Helper macros for async/await syntax */
#define UVRPC_ASYNC(ctx, timeout_ms) \
    { \
        int _async_status = uvrpc_async_exec((ctx), (timeout_ms)); \
        if (_async_status != UVRPC_OK) { \
            fprintf(stderr, "Async exec failed: %d\n", _async_status); \
            goto async_cleanup; \
        } \
    }

#define UVRPC_AWAIT(call) \
    do { \
        int _await_status = (call); \
        if (_await_status != UVRPC_OK) { \
            fprintf(stderr, "Await failed: %d\n", _await_status); \
            goto async_cleanup; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_ASYNC_H */