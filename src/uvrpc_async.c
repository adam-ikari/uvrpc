/**
 * UVRPC Async/Await Implementation
 * Uses libuv co-routines with setjmp/longjmp
 */

#include "../include/uvrpc_async.h"
#include "../include/uvrpc_allocator.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define UVRPC_MAX_ASYNC_CALLS 100

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