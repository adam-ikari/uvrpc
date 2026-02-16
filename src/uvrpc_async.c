/**
 * UVRPC Async/Await Implementation
 * Uses libuv co-routines with setjmp/longjmp
 */

#include "../include/uvrpc_async.h"
#include "../include/uvrpc_allocator.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define UVRPC_MAX_ASYNC_CALLS 100
#define UVRPC_MAX_CONCURRENT_CALLS 20

/* Retry context for timer-based retry */
typedef struct retry_context {
    uvrpc_client_t* client;
    char* method;
    uint8_t* params;
    size_t params_size;
    uvrpc_async_result_t** result;
    int attempt;
    int max_retries;
    uint64_t retry_delay_ms;
    uvrpc_async_ctx_t* ctx;
    uv_timer_t timer;
} retry_context_t;

/* Timer callback for retry delay */
static void retry_delay_callback(uv_timer_t* handle) {
    retry_context_t* retry_ctx = (retry_context_t*)handle->data;

    /* Retry the call */
    uvrpc_client_call_async(retry_ctx->ctx, retry_ctx->client, retry_ctx->method,
                           retry_ctx->params, retry_ctx->params_size, retry_ctx->result);

    /* Cleanup retry context */
    uvrpc_free(retry_ctx->method);
    uvrpc_free(retry_ctx->params);
    uvrpc_free(retry_ctx);
}

/* Async context structure */
struct uvrpc_async_ctx {
    uv_loop_t* loop;
    jmp_buf jmp_buf;
    int is_running;
    int call_count;
    uvrpc_async_result_t* pending_results[UVRPC_MAX_ASYNC_CALLS];
    int current_call;
    uint64_t timeout_ms;
    uv_timer_t timeout_timer;
    int timed_out;
    
    /* For concurrent operations */
    int concurrent_count;
    int completed_count;
    int concurrent_indices[UVRPC_MAX_CONCURRENT_CALLS];
    uvrpc_async_result_t* concurrent_results[UVRPC_MAX_CONCURRENT_CALLS];
    int any_completed;
    jmp_buf any_jmp_buf;
};

/* Internal wrapper for response callback */
static void async_response_callback(uvrpc_response_t* resp, void* ctx) {
    uvrpc_async_ctx_t* async_ctx = (uvrpc_async_ctx_t*)ctx;
    
    if (async_ctx->current_call < 0 || async_ctx->current_call >= UVRPC_MAX_ASYNC_CALLS) {
        return;
    }
    
    /* Store result */
    uvrpc_async_result_t* result = uvrpc_alloc(sizeof(uvrpc_async_result_t));
    if (!result) {
        longjmp(async_ctx->jmp_buf, UVRPC_ERROR_NO_MEMORY);
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
    
    /* Jump back to continue execution */
    longjmp(async_ctx->jmp_buf, UVRPC_OK);
}

/* Timeout callback */
static void timeout_callback(uv_timer_t* handle) {
    uvrpc_async_ctx_t* async_ctx = (uvrpc_async_ctx_t*)handle->data;
    async_ctx->timed_out = 1;
    longjmp(async_ctx->jmp_buf, UVRPC_ERROR_TIMEOUT);
}

/* Create async context */
uvrpc_async_ctx_t* uvrpc_async_ctx_new(uv_loop_t* loop) {
    if (!loop) return NULL;
    
    uvrpc_async_ctx_t* ctx = uvrpc_alloc(sizeof(uvrpc_async_ctx_t));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(uvrpc_async_ctx_t));
    ctx->loop = loop;
    ctx->is_running = 0;
    ctx->call_count = 0;
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
        uv_timer_init(ctx->loop, &ctx->timeout_timer);
        ctx->timeout_timer.data = ctx;
        uv_timer_start(&ctx->timeout_timer, timeout_callback, timeout_ms, 0);
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
    
    /* Save jump point */
    int jump_status = setjmp(ctx->jmp_buf);
    
    if (jump_status == 0) {
        /* First call: make RPC request */
        int call_status = uvrpc_client_call(client, method, params, params_size,
                                             async_response_callback, ctx);
        if (call_status != UVRPC_OK) {
            ctx->current_call = -1;
            return call_status;
        }
        
        /* Run event loop to wait for response */
        while (ctx->pending_results[slot] == NULL && !ctx->timed_out) {
            uv_run(ctx->loop, UV_RUN_ONCE);
        }
        
        /* This shouldn't happen, but handle it */
        ctx->current_call = -1;
        return UVRPC_ERROR_TIMEOUT;
    } else {
        /* Jump back from callback */
        ctx->current_call = -1;
        
        if (jump_status == UVRPC_ERROR_TIMEOUT) {
            /* Handle timeout */
            if (ctx->timeout_ms > 0) {
                uv_timer_stop(&ctx->timeout_timer);
                uv_close((uv_handle_t*)&ctx->timeout_timer, NULL);
            }
            return UVRPC_ERROR_TIMEOUT;
        }
        
        /* Return result */
        *result = ctx->pending_results[slot];
        ctx->pending_results[slot] = NULL;
        
        return UVRPC_OK;
    }
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
    
    /* For any() - jump back immediately */
    if (!async_ctx->any_completed && async_ctx->completed_count >= 1) {
        async_ctx->any_completed = 1;
        longjmp(async_ctx->any_jmp_buf, UVRPC_OK);
    }
}

/* Timeout callback for all() */
static void all_timeout_callback(uv_timer_t* handle) {
    int* timed_out = (int*)handle->data;
    *timed_out = 1;
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
    uv_timer_t timeout_timer;
    int timed_out = 0;
    if (timeout_ms > 0) {
        uv_timer_init(ctx->loop, &timeout_timer);
        timeout_timer.data = &timed_out;
        uv_timer_start(&timeout_timer, all_timeout_callback, timeout_ms, 0);
    }
    
    /* Start all concurrent calls */
    for (int i = 0; i < count; i++) {
        uvrpc_response_t* resp_wrapper = uvrpc_alloc(sizeof(uvrpc_response_t));
        resp_wrapper->user_data = (void*)(intptr_t)i;
        
        uvrpc_client_call(clients[i], methods[i], params_array[i], params_sizes[i],
                          concurrent_callback, ctx);
    }
    
    /* Wait for all to complete */
    while (ctx->completed_count < count && !timed_out) {
        uv_run(ctx->loop, UV_RUN_ONCE);
    }
    
    /* Stop timeout */
    if (timeout_ms > 0) {
        uv_timer_stop(&timeout_timer);
        uv_close((uv_handle_t*)&timeout_timer, NULL);
    }
    
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

/* Timeout callback for any() */
static void any_timeout_callback(uv_timer_t* handle) {
    int* timed_out = (int*)handle->data;
    *timed_out = 1;
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
    uv_timer_t timeout_timer;
    int timed_out = 0;
    if (timeout_ms > 0) {
        uv_timer_init(ctx->loop, &timeout_timer);
        timeout_timer.data = &timed_out;
        uv_timer_start(&timeout_timer, any_timeout_callback, timeout_ms, 0);
    }
    
    /* Set jump point */
    int jump_status = setjmp(ctx->any_jmp_buf);
    
    if (jump_status == 0) {
        /* Start all concurrent calls */
        for (int i = 0; i < count; i++) {
            uvrpc_client_call(clients[i], methods[i], params_array[i], params_sizes[i],
                              concurrent_callback, ctx);
        }
        
        /* Wait for any to complete */
        while (!ctx->any_completed && !timed_out) {
            uv_run(ctx->loop, UV_RUN_ONCE);
        }
        
        /* Check if timeout occurred */
        if (timed_out) {
            if (timeout_ms > 0) {
                uv_timer_stop(&timeout_timer);
                uv_close((uv_handle_t*)&timeout_timer, NULL);
            }
            return UVRPC_ERROR_TIMEOUT;
        }
        
        /* Shouldn't reach here normally */
        return UVRPC_ERROR_TIMEOUT;
    } else {
        /* Jumped back from callback or timeout */
        
        if (timeout_ms > 0) {
            uv_timer_stop(&timeout_timer);
            uv_close((uv_handle_t*)&timeout_timer, NULL);
        }
        
        if (jump_status == UVRPC_ERROR_TIMEOUT) {
            return UVRPC_ERROR_TIMEOUT;
        }
        
        /* Find completed result */
        for (int i = 0; i < count; i++) {
            if (ctx->concurrent_results[i]) {
                *result = ctx->concurrent_results[i];
                *completed_index = i;
                ctx->concurrent_results[i] = NULL;
                break;
            }
        }
        
        return UVRPC_OK;
    }
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
    
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int status = uvrpc_client_call_async(ctx, client, method, params, params_size, result);
        
        if (status == UVRPC_OK && *result && (*result)->error_code == 0) {
            return UVRPC_OK;
        }
        
        if (*result) {
            uvrpc_async_result_free(*result);
            *result = NULL;
        }
        
        if (attempt < max_retries && retry_delay_ms > 0) {
            /* Non-blocking delay using timer - return to caller, retry will be triggered by timer */
            /* Store retry context for timer callback */
            retry_context_t* retry_ctx = uvrpc_alloc(sizeof(retry_context_t));
            if (!retry_ctx) {
                return UVRPC_ERROR;
            }
            retry_ctx->client = client;
            retry_ctx->method = uvrpc_strdup(method);
            retry_ctx->params = uvrpc_alloc(params_size);
            if (!retry_ctx->method || !retry_ctx->params) {
                uvrpc_free(retry_ctx->method);
                uvrpc_free(retry_ctx->params);
                uvrpc_free(retry_ctx);
                return UVRPC_ERROR;
            }
            memcpy(retry_ctx->params, params, params_size);
            retry_ctx->params_size = params_size;
            retry_ctx->result = result;
            retry_ctx->attempt = attempt;
            retry_ctx->max_retries = max_retries;
            retry_ctx->retry_delay_ms = retry_delay_ms;
            retry_ctx->ctx = ctx;
            
            uv_timer_init(ctx->loop, &retry_ctx->timer);
            retry_ctx->timer.data = retry_ctx;
            uv_timer_start(&retry_ctx->timer, retry_delay_callback, retry_delay_ms, 0);
            
            return UVRPC_ERROR;  /* Will retry via timer */
        }
    }
    
    return UVRPC_ERROR;
}

/* Promise.timeout - call with timeout */
int uvrpc_async_timeout(uvrpc_async_ctx_t* ctx, uvrpc_client_t* client,
                         const char* method, const uint8_t* params,
                         size_t params_size, uvrpc_async_result_t** result,
                         uint64_t timeout_ms) {
    if (!ctx || !client || !method || !result) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* Start the call */
    int started = setjmp(ctx->jmp_buf);
    
    if (started == 0) {
        /* Setup timeout */
        if (timeout_ms > 0) {
            uv_timer_init(ctx->loop, &ctx->timeout_timer);
            ctx->timeout_timer.data = ctx;
            ctx->timed_out = 0;
            uv_timer_start(&ctx->timeout_timer, timeout_callback, timeout_ms, 0);
        }

        /* Make the call */
        return uvrpc_client_call_async(ctx, client, method, params, params_size, result);
    } else {
        /* Jumped from timeout */
        if (ctx->timeout_ms > 0) {
            uv_timer_stop(&ctx->timeout_timer);
            uv_close((uv_handle_t*)&ctx->timeout_timer, NULL);
        }
        return UVRPC_ERROR_TIMEOUT;
    }
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

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int status = uvrpc_client_call_async(ctx, client, method, params, params_size, result);

        if (status == UVRPC_OK && *result && (*result)->error_code == 0) {
            return UVRPC_OK;
        }

        if (*result) {
            uvrpc_async_result_free(*result);
            *result = NULL;
        }

        if (attempt < max_retries) {
            /* Calculate delay with exponential backoff */
            uint64_t delay_ms = (uint64_t)(initial_delay_ms * pow(backoff_multiplier, attempt));

            /* Non-blocking delay using timer */
            retry_context_t* retry_ctx = uvrpc_alloc(sizeof(retry_context_t));
            if (!retry_ctx) {
                return UVRPC_ERROR;
            }
            retry_ctx->client = client;
            retry_ctx->method = uvrpc_strdup(method);
            retry_ctx->params = uvrpc_alloc(params_size);
            if (!retry_ctx->method || !retry_ctx->params) {
                uvrpc_free(retry_ctx->method);
                uvrpc_free(retry_ctx->params);
                uvrpc_free(retry_ctx);
                return UVRPC_ERROR;
            }
            memcpy(retry_ctx->params, params, params_size);
            retry_ctx->params_size = params_size;
            retry_ctx->result = result;
            retry_ctx->attempt = attempt;
            retry_ctx->max_retries = max_retries;
            retry_ctx->retry_delay_ms = delay_ms;
            retry_ctx->ctx = ctx;

            uv_timer_init(ctx->loop, &retry_ctx->timer);
            retry_ctx->timer.data = retry_ctx;
            uv_timer_start(&retry_ctx->timer, retry_delay_callback, delay_ms, 0);

            return UVRPC_ERROR;  /* Will retry via timer */
        }
    }

    return UVRPC_ERROR;
}

/* Cancel all pending async operations */
int uvrpc_async_cancel_all(uvrpc_async_ctx_t* ctx) {
    if (!ctx) return UVRPC_ERROR_INVALID_PARAM;

    /* Stop timeout timer if running */
    if (ctx->timeout_ms > 0 && !uv_is_closing((uv_handle_t*)&ctx->timeout_timer)) {
        uv_timer_stop(&ctx->timeout_timer);
        uv_close((uv_handle_t*)&ctx->timeout_timer, NULL);
    }

    /* Free all pending results */
    for (int i = 0; i < UVRPC_MAX_ASYNC_CALLS; i++) {
        if (ctx->pending_results[i]) {
            uvrpc_async_result_free(ctx->pending_results[i]);
            ctx->pending_results[i] = NULL;
        }
    }

    /* Reset state */
    ctx->call_count = 0;
    ctx->current_call = -1;
    ctx->is_running = 0;

    return UVRPC_OK;
}

/* Get pending async operation count */
int uvrpc_async_get_pending_count(uvrpc_async_ctx_t* ctx) {
    if (!ctx) return 0;

    int count = 0;
    for (int i = 0; i < UVRPC_MAX_ASYNC_CALLS; i++) {
        if (ctx->pending_results[i] != NULL) {
            count++;
        }
    }
    return count;
}