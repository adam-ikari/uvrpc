#ifndef UVRPC_INTERNAL_H
#define UVRPC_INTERNAL_H

#include "uvrpc.h"
#include <uvzmq.h>
#include <uthash.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 服务处理器条目 */
typedef struct uvrpc_service_entry {
    char* name;                         /* 服务名称 */
    uvrpc_service_handler_t handler;    /* 处理函数 */
    void* ctx;                          /* 用户上下文 */
    UT_hash_handle hh;                  /* uthash 句柄 */
} uvrpc_service_entry_t;

/* RPC 服务器结构 */
struct uvrpc_server {
    uv_loop_t* loop;                    /* libuv 事件循环 */
    char* bind_addr;                    /* 绑定地址 */
    void* zmq_ctx;                      /* ZMQ context */
    void* zmq_sock;                     /* ZMQ socket */
    uvzmq_socket_t* socket;             /* uvzmq socket */
    int zmq_type;                       /* ZMQ socket 类型 */
    uvrpc_service_entry_t* services;    /* 服务注册表 */
    int owns_loop;                      /* 是否拥有 loop 的所有权 */
    int is_running;                     /* 运行状态 */
};

/* 客户端请求上下文 */
typedef struct uvrpc_client_request {
    uint32_t request_id;               /* 请求 ID */
    uvrpc_response_callback_t callback; /* 响应回调 */
    void* ctx;                          /* 用户上下文 */
    UT_hash_handle hh;                  /* uthash 句柄 */
} uvrpc_client_request_t;

/* RPC 客户端结构 */
struct uvrpc_client {
    uv_loop_t* loop;                    /* libuv 事件循环 */
    char* server_addr;                  /* 服务器地址 */
    void* zmq_ctx;                      /* ZMQ context */
    void* zmq_sock;                     /* ZMQ socket */
    uvzmq_socket_t* socket;             /* uvzmq socket */
    int zmq_type;                       /* ZMQ socket 类型 */
    uvrpc_client_request_t* pending_requests; /* 待处理请求 */
    int owns_loop;                      /* 是否拥有 loop 的所有权 */
    uint32_t next_request_id;           /* 下一个请求 ID */
    int is_connected;                   /* 连接状态 */
};

/* 内存分配器宏 */
#define UVRPC_MALLOC(size)      malloc(size)
#define UVRPC_CALLOC(n, size)   calloc(n, size)
#define UVRPC_REALLOC(ptr, size) realloc(ptr, size)
#define UVRPC_FREE(ptr)         do { if (ptr) { free(ptr); ptr = NULL; } } while(0)

/* 日志宏 */
#ifdef UVRPC_DEBUG
    #define UVRPC_LOG_DEBUG(fmt, ...) fprintf(stderr, "[UVRPC DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
    #define UVRPC_LOG_DEBUG(fmt, ...) ((void)0)
#endif

#define UVRPC_LOG_ERROR(fmt, ...) fprintf(stderr, "[UVRPC ERROR] " fmt "\n", ##__VA_ARGS__)
#define UVRPC_LOG_INFO(fmt, ...)  fprintf(stderr, "[UVRPC INFO] " fmt "\n", ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_INTERNAL_H */