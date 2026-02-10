#include "uvrpc_internal.h"
#include "../include/uvrpc.h"
#include <string.h>

/* 异步状态常量 */
#define UVRPC_ASYNC_IDLE     0
#define UVRPC_ASYNC_PENDING  1
#define UVRPC_ASYNC_DONE     2

/* 异步回调函数 */
static void async_callback(void* ctx, int status,
                            const uint8_t* response_data,
                            size_t response_size) {
    uvrpc_async_t* async = (uvrpc_async_t*)ctx;
    
    /* 单线程模型，无需锁保护 */
    async->state = UVRPC_ASYNC_DONE;
    async->result.status = status;
    async->result.response_data = response_data;
    async->result.response_size = response_size;
}

/* 创建异步调用上下文 */
uvrpc_async_t* uvrpc_async_new(uv_loop_t* loop) {
    if (!loop) {
        return NULL;
    }
    
    uvrpc_async_t* async = (uvrpc_async_t*)UVRPC_MALLOC(sizeof(uvrpc_async_t));
    if (!async) {
        return NULL;
    }
    
    memset(async, 0, sizeof(uvrpc_async_t));
    async->loop = loop;
    async->state = UVRPC_ASYNC_IDLE;
    
    return async;
}

/* 释放异步调用上下文 */
void uvrpc_async_free(uvrpc_async_t* async) {
    if (!async) {
        return;
    }
    
    UVRPC_FREE(async);
}

/* 异步调用 RPC 服务 */
int uvrpc_client_call_async(uvrpc_client_t* client,
                             const char* service_name,
                             const char* method_name,
                             const uint8_t* request_data,
                             size_t request_size,
                             uvrpc_async_t* async) {
    if (!client || !service_name || !method_name || !async) {
        return UVRPC_ERROR_INVALID_PARAM;
    }
    
    /* 重置状态 */
    async->state = UVRPC_ASYNC_PENDING;
    memset(&async->result, 0, sizeof(async->result));
    
    return uvrpc_client_call(client, service_name, method_name,
                              request_data, request_size,
                              async_callback, async);
}

/* 等待异步调用完成 */
const uvrpc_async_result_t* uvrpc_await(uvrpc_async_t* async) {
    if (!async) {
        static uvrpc_async_result_t error_result = {
            .status = UVRPC_ERROR_INVALID_PARAM,
            .response_data = NULL,
            .response_size = 0
        };
        return &error_result;
    }
    
    /* 运行事件循环直到完成 */
    while (async->state == UVRPC_ASYNC_PENDING) {
        uv_run(async->loop, UV_RUN_ONCE);
    }
    
    return &async->result;
}

/* 带超时的等待 */
const uvrpc_async_result_t* uvrpc_await_timeout(uvrpc_async_t* async, uint64_t timeout_ms) {
    if (!async) {
        static uvrpc_async_result_t error_result = {
            .status = UVRPC_ERROR_INVALID_PARAM,
            .response_data = NULL,
            .response_size = 0
        };
        return &error_result;
    }
    
    uint64_t deadline = uv_now(async->loop) + timeout_ms;
    
    while (async->state == UVRPC_ASYNC_PENDING) {
        if (uv_now(async->loop) >= deadline) {
            static uvrpc_async_result_t timeout_result = {
                .status = UVRPC_ERROR_TIMEOUT,
                .response_data = NULL,
                .response_size = 0
            };
            return &timeout_result;
        }
        
        uv_run(async->loop, UV_RUN_ONCE);
    }
    
    return &async->result;
}