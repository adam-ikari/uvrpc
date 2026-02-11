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
    
    /* 释放之前的数据（如果有） */
    if (async->result.response_data) {
        free((void*)async->result.response_data);
        async->result.response_data = NULL;
    }
    
    /* 复制响应数据，因为 response_data 会在回调返回后被释放 */
    if (response_data && response_size > 0) {
        async->result.response_data = (const uint8_t*)UVRPC_MALLOC(response_size);
        if (async->result.response_data) {
            memcpy((void*)async->result.response_data, response_data, response_size);
            async->result.response_size = response_size;
        } else {
            async->result.response_data = NULL;
            async->result.response_size = 0;
            async->result.status = UVRPC_ERROR_NO_MEMORY;
        }
    } else {
        async->result.response_data = NULL;
        async->result.response_size = 0;
    }
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
    
    /* 释放复制的响应数据 */
    if (async->result.response_data) {
        free((void*)async->result.response_data);
        async->result.response_data = NULL;
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
    
    /* 释放之前复制的响应数据（如果有） */
    if (async->result.response_data) {
        free((void*)async->result.response_data);
        async->result.response_data = NULL;
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

/* 等待所有异步调用完成 */
int uvrpc_await_all(uvrpc_async_t** asyncs, int count) {
    if (!asyncs || count <= 0) {
        return 0;
    }
    
    if (count == 1) {
        /* 单个请求，使用简单的 await */
        uvrpc_await(asyncs[0]);
        return (asyncs[0]->result.status == UVRPC_OK) ? 1 : 0;
    }
    
    uv_loop_t* loop = asyncs[0]->loop;
    
    /* 运行事件循环直到所有请求完成 */
    while (1) {
        /* 检查是否所有请求都已完成 */
        int all_done = 1;
        for (int i = 0; i < count; i++) {
            if (asyncs[i]->state == UVRPC_ASYNC_PENDING) {
                all_done = 0;
                break;
            }
        }
        
        if (all_done) {
            break;
        }
        
        uv_run(loop, UV_RUN_ONCE);
    }
    
    /* 返回成功的请求数 */
    int success_count = 0;
    for (int i = 0; i < count; i++) {
        if (asyncs[i]->result.status == UVRPC_OK) {
            success_count++;
        }
    }
    
    return success_count;
}

/* 等待任意一个异步调用完成 */
int uvrpc_await_any(uvrpc_async_t** asyncs, int count) {
    if (!asyncs || count <= 0) {
        return -1;
    }
    
    if (count == 1) {
        return (uvrpc_await(asyncs[0])->status == UVRPC_OK) ? 0 : -1;
    }
    
    uv_loop_t* loop = asyncs[0]->loop;
    
    /* 检查是否已有完成的请求 */
    for (int i = 0; i < count; i++) {
        if (asyncs[i]->state == UVRPC_ASYNC_DONE) {
            return i;
        }
    }
    
    /* 运行事件循环直到任意一个请求完成 */
    while (1) {
        for (int i = 0; i < count; i++) {
            if (asyncs[i]->state == UVRPC_ASYNC_DONE) {
                return i;
            }
        }
        
        uv_run(loop, UV_RUN_ONCE);
    }
    
    return -1;  /* 不应该到达这里 */
}