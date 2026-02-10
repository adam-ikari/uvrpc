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
    
    /* ROUTER 模式多部分消息状态 */
    uint8_t routing_id[256];            /* 存储客户端路由标识 */
    size_t routing_id_size;             /* 路由标识大小 */
    int has_routing_id;                 /* 是否已接收到路由帧 */
    int router_state;                   /* ROUTER 状态：0=等待空帧, 1=等待数据帧 */
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

/* 内存分配器配置
 * 
 * 通过编译宏选择内存分配器：
 * - 默认：系统 malloc/free
 * - UVRPC_USE_MIMALLOC：使用 mimalloc（高性能、低碎片）
 * - UVRPC_USE_CUSTOM_ALLOCATOR：使用自定义分配器（需定义 UVRPC_CUSTOM_MALLOC/FREE）
 */
#ifndef UVRPC_USE_MIMALLOC
#ifndef UVRPC_USE_CUSTOM_ALLOCATOR
/* 默认使用系统分配器 */
#include <stdlib.h>
#define UVRPC_MALLOC(size)      malloc(size)
#define UVRPC_CALLOC(n, size)   calloc(n, size)
#define UVRPC_REALLOC(ptr, size) realloc(ptr, size)
#define UVRPC_FREE(ptr)         do { if (ptr) { free(ptr); ptr = NULL; } } while(0)
#else
/* 自定义分配器 */
#ifdef UVRPC_CUSTOM_MALLOC
#define UVRPC_MALLOC(size)      UVRPC_CUSTOM_MALLOC(size)
#else
#error "UVRPC_USE_CUSTOM_ALLOCATOR defined but UVRPC_CUSTOM_MALLOC not defined"
#endif
#ifdef UVRPC_CUSTOM_CALLOC
#define UVRPC_CALLOC(n, size)   UVRPC_CUSTOM_CALLOC(n, size)
#else
#error "UVRPC_USE_CUSTOM_ALLOCATOR defined but UVRPC_CUSTOM_CALLOC not defined"
#endif
#ifdef UVRPC_CUSTOM_REALLOC
#define UVRPC_REALLOC(ptr, size) UVRPC_CUSTOM_REALLOC(ptr, size)
#else
#error "UVRPC_USE_CUSTOM_ALLOCATOR defined but UVRPC_CUSTOM_REALLOC not defined"
#endif
#ifdef UVRPC_CUSTOM_FREE
#define UVRPC_FREE(ptr)         do { if (ptr) { UVRPC_CUSTOM_FREE(ptr); ptr = NULL; } } while(0)
#else
#error "UVRPC_USE_CUSTOM_ALLOCATOR defined but UVRPC_CUSTOM_FREE not defined"
#endif
#endif
#else
/* 使用 mimalloc */
#include <mimalloc.h>
#define UVRPC_MALLOC(size)      mi_malloc(size)
#define UVRPC_CALLOC(n, size)   mi_calloc(n, size)
#define UVRPC_REALLOC(ptr, size) mi_realloc(ptr, size)
#define UVRPC_FREE(ptr)         do { if (ptr) { mi_free(ptr); ptr = NULL; } } while(0)
#endif

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