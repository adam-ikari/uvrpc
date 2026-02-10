#include "uvrpc_internal.h"
#include "../include/uvrpc.h"
#include <string.h>
#include <unistd.h>

/* 异步回调函数 */
static void async_callback(void* ctx, int status,
                           const uint8_t* response_data,
                           size_t response_size) {
    uvrpc_async_t* async = (uvrpc_async_t*)ctx;
    
    async->result.status = status;
    async->result.response_data = response_data;
    async->result.response_size = response_size;
    async->state = UVRPC_ASYNC_DONE;
    
    /* 通知等待线程 */
    uv_async_send(&async->async_handle);
}

/* uv_async 回调（用于唤醒事件循环） */
static void async_wakeup(uv_async_t* handle) {
    /* 空实现，仅用于唤醒事件循环 */
    (void)handle;
}

/* 创建异步调用上下文 */
uvrpc_async_t* uvrpc_async_new(uv_loop_t* loop) {
    uvrpc_async_t* async = (uvrpc_async_t*)UVRPC_MALLOC(sizeof(uvrpc_async_t));
    if (!async) {
        return NULL;
    }
    
    memset(async, 0, sizeof(uvrpc_async_t));
    async->state = UVRPC_ASYNC_PENDING;
    async->loop = loop;
    
    if (uv_async_init(loop, &async->async_handle, async_wakeup) != 0) {
        UVRPC_FREE(async);
        return NULL;
    }
    
    return async;
}

/* 释放异步调用上下文 */
void uvrpc_async_free(uvrpc_async_t* async) {
    if (!async) {
        return;
    }
    
    uv_close((uv_handle_t*)&async->async_handle, NULL);
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
    
    /* 使用 UV_RUN_NOWAIT 配合短睡眠来轮询状态 */
    while (async->state == UVRPC_ASYNC_PENDING) {
        uv_run(async->loop, UV_RUN_NOWAIT);
        usleep(1000); /* 1ms */
    }
    
    return &async->result;
}