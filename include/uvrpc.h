#ifndef UVRPC_H
#define UVRPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <uv.h>
#include <zmq.h>

/* ==================== 类型定义 ==================== */

/* 传输类型 */
typedef enum {
    UVRPC_TRANSPORT_INPROC = 0,  /* 进程内通信 */
    UVRPC_TRANSPORT_IPC = 1,     /* 本地进程间通信 */
    UVRPC_TRANSPORT_TCP = 2,     /* TCP网络通信 */
    UVRPC_TRANSPORT_UDP = 3      /* UDP网络通信 */
} uvrpc_transport_t;

/* 模式 */
typedef enum {
    UVRPC_SERVER_CLIENT = 0,  /* 服务器-客户端 (ROUTER/DEALER) */
    UVRPC_BROADCAST = 1         /* 广播 (PUB/SUB) */
} uvrpc_mode_t;

/* 性能模式 */
typedef enum {
    UVRPC_PERF_LOW_LATENCY = 0,    /* 低延迟模式 */
    UVRPC_PERF_BALANCED = 1,        /* 平衡模式 */
    UVRPC_PERF_HIGH_THROUGHPUT = 2  /* 高吞吐模式 */
} uvrpc_performance_mode_t;

/* 前向声明 */
typedef struct uvrpc_server uvrpc_server_t;
typedef struct uvrpc_client uvrpc_client_t;
typedef struct uvrpc_async uvrpc_async_t;

/* 服务处理函数 */
typedef int (*uvrpc_service_handler_t)(void* ctx,
                                         const uint8_t* request_data,
                                         size_t request_size,
                                         uint8_t** response_data,
                                         size_t* response_size);

/* 客户端响应回调 */
typedef void (*uvrpc_response_callback_t)(void* ctx,
                                            int status,
                                            const uint8_t* response_data,
                                            size_t response_size);

/* 异步结果 */
typedef struct uvrpc_async_result {
    int status;                      /* 状态码 */
    const uint8_t* response_data;    /* 响应数据 */
    size_t response_size;            /* 响应数据大小 */
} uvrpc_async_result_t;

/* ==================== 配置结构 ==================== */

/**
 * UVRPC 统一配置结构
 */
typedef struct {
    /* 基本信息 */
    uv_loop_t* loop;                 /* libuv 事件循环 */
    char* address;                   /* 绑定/连接地址 */
    uvrpc_transport_t transport;     /* 传输类型 */
    uvrpc_mode_t mode;               /* 模式 */

    /* ZMQ 配置 */
    void* zmq_ctx;                   /* ZMQ context (NULL=自动创建) */
    int owns_zmq_ctx;               /* 是否拥有 context 所有权 */

    /* 性能配置 */
    uvrpc_performance_mode_t perf_mode;  /* 性能模式 */
    int batch_size;                  /* 批量大小 */
    int io_threads;                  /* I/O 线程数 */
    int sndhwm;                      /* 发送高水位 */
    int rcvhwm;                      /* 接收高水位 */
    int tcp_sndbuf;                  /* TCP 发送缓冲区 */
    int tcp_rcvbuf;                  /* TCP 接收缓冲区 */
    int tcp_keepalive;               /* TCP keepalive */
    int tcp_keepalive_idle;          /* TCP keepalive 空闲时间 */
    int tcp_keepalive_cnt;           /* TCP keepalive 探测次数 */
    int tcp_keepalive_intvl;         /* TCP keepalive 探测间隔 */
    int reconnect_ivl;               /* 重连间隔 */
    int reconnect_ivl_max;           /* 最大重连间隔 */
    int linger;                      /* Linger 时间 */

    /* UDP 特定配置 */
    int udp_multicast;               /* 是否组播 */
    char* udp_multicast_group;       /* 组播组地址 */
} uvrpc_config_t;

/* ==================== 配置构建器 API ==================== */

/**
 * 创建配置结构
 */
uvrpc_config_t* uvrpc_config_new(void);

/**
 * 释放配置结构
 */
void uvrpc_config_free(uvrpc_config_t* config);

/**
 * 设置事件循环
 */
uvrpc_config_t* uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);

/**
 * 设置地址
 */
uvrpc_config_t* uvrpc_config_set_address(uvrpc_config_t* config, const char* address);

/**
 * 设置传输类型
 */
uvrpc_config_t* uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_t transport);

/**
 * 设置模式
 */
uvrpc_config_t* uvrpc_config_set_mode(uvrpc_config_t* config, uvrpc_mode_t mode);

/**
 * 设置 ZMQ context
 */
uvrpc_config_t* uvrpc_config_set_zmq_ctx(uvrpc_config_t* config, void* zmq_ctx);

/**
 * 设置性能模式（自动配置所有性能参数）
 */
uvrpc_config_t* uvrpc_config_set_perf_mode(uvrpc_config_t* config, uvrpc_performance_mode_t mode);

/**
 * 设置批量大小
 */
uvrpc_config_t* uvrpc_config_set_batch_size(uvrpc_config_t* config, int batch_size);

/**
 * 设置高水位标记
 */
uvrpc_config_t* uvrpc_config_set_hwm(uvrpc_config_t* config, int sndhwm, int rcvhwm);

/**
 * 设置 I/O 线程数
 */
uvrpc_config_t* uvrpc_config_set_io_threads(uvrpc_config_t* config, int io_threads);

/**
 * 设置 TCP 缓冲区
 */
uvrpc_config_t* uvrpc_config_set_tcp_buffer(uvrpc_config_t* config, int sndbuf, int rcvbuf);

/**
 * 设置 TCP keepalive
 */
uvrpc_config_t* uvrpc_config_set_tcp_keepalive(uvrpc_config_t* config, int enable, int idle, int cnt, int intvl);

/**
 * 设置重连间隔
 */
uvrpc_config_t* uvrpc_config_set_reconnect(uvrpc_config_t* config, int ivl, int ivl_max);

/**
 * 设置 Linger
 */
uvrpc_config_t* uvrpc_config_set_linger(uvrpc_config_t* config, int linger_ms);

/**
 * 设置 UDP 组播
 */
uvrpc_config_t* uvrpc_config_set_udp_multicast(uvrpc_config_t* config, const char* group);

/* ==================== 服务器 API ==================== */

/**
 * 创建服务器（使用配置）
 */
uvrpc_server_t* uvrpc_server_create(const uvrpc_config_t* config);

/**
 * 注册服务
 */
int uvrpc_server_register_service(uvrpc_server_t* server,
                                   const char* service_name,
                                   uvrpc_service_handler_t handler,
                                   void* ctx);

/**
 * 启动服务器
 */
int uvrpc_server_start(uvrpc_server_t* server);

/**
 * 停止服务器
 */
int uvrpc_server_stop(uvrpc_server_t* server);

/**
 * 获取统计信息
 */
int uvrpc_server_get_stats(uvrpc_server_t* server, int* services_count);

/**
 * 释放服务器
 */
void uvrpc_server_free(uvrpc_server_t* server);

/* ==================== 客户端 API ==================== */

/**
 * 创建客户端（使用配置）
 */
uvrpc_client_t* uvrpc_client_create(const uvrpc_config_t* config);

/**
 * 连接服务器
 */
int uvrpc_client_connect(uvrpc_client_t* client);

/**
 * 断开连接
 */
void uvrpc_client_disconnect(uvrpc_client_t* client);

/**
 * 异步调用
 */
int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_name,
                       const char* method_name,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx);

/**
 * 获取统计信息
 */
int uvrpc_client_get_stats(uvrpc_client_t* client, int* pending_requests);

/**
 * 释放客户端
 */
void uvrpc_client_free(uvrpc_client_t* client);

/* ==================== Async API ==================== */

/**
 * 创建 async 上下文
 */
uvrpc_async_t* uvrpc_async_create(uv_loop_t* loop);

/**
 * 释放 async 上下文
 */
void uvrpc_async_free(uvrpc_async_t* async);

/**
 * Async 调用
 */
int uvrpc_client_call_async(uvrpc_client_t* client,
                             const char* service_name,
                             const char* method_name,
                             const uint8_t* request_data,
                             size_t request_size,
                             uvrpc_async_t* async);

/**
 * Await 等待完成
 */
const uvrpc_async_result_t* uvrpc_async_await(uvrpc_async_t* async);

/**
 * Await 等待完成（带超时）
 */
const uvrpc_async_result_t* uvrpc_async_await_timeout(uvrpc_async_t* async, uint64_t timeout_ms);

/**
 * Await 等待所有完成
 */
int uvrpc_async_await_all(uvrpc_async_t** asyncs, int count);

/**
 * Await 等待任意一个完成
 */
int uvrpc_async_await_any(uvrpc_async_t** asyncs, int count);

/* ==================== 通用客户端调用 API ==================== */

/**
 * 序列化函数类型
 * 用于生成的代码的通用调用框架
 */
typedef int (*uvrpc_serialize_func_t)(const void* request, uint8_t** data, size_t* size);

/**
 * 反序列化函数类型
 */
typedef int (*uvrpc_deserialize_func_t)(const uint8_t* data, size_t size, void* response);

/**
 * 释放函数类型
 */
typedef void (*uvrpc_free_func_t)(void* obj);

/**
 * 通用客户端调用（同步等待）
 * 
 * @param client 客户端对象
 * @param service_name 服务名称
 * @param method_name 方法名称
 * @param request 请求对象
 * @param serialize_func 序列化函数
 * @param response 响应对象
 * @param deserialize_func 反序列化函数
 * @param loop 事件循环
 * @return UVRPC_OK 成功，其他值表示错误
 */
int uvrpc_client_call_sync(
    uvrpc_client_t* client,
    const char* service_name,
    const char* method_name,
    const void* request,
    uvrpc_serialize_func_t serialize_func,
    void* response,
    uvrpc_deserialize_func_t deserialize_func,
    uv_loop_t* loop
);

/**
 * 通用客户端异步调用（带超时）
 * 
 * @param client 客户端对象
 * @param service_name 服务名称
 * @param method_name 方法名称
 * @param request 请求对象
 * @param serialize_func 序列化函数
 * @param async 异步上下文
 * @return UVRPC_OK 成功，其他值表示错误
 */
int uvrpc_client_call_async_generic(
    uvrpc_client_t* client,
    const char* service_name,
    const char* method_name,
    const void* request,
    uvrpc_serialize_func_t serialize_func,
    uvrpc_async_t* async
);

/* ==================== 错误码 ==================== */

#define UVRPC_OK                0
#define UVRPC_ERROR             -1
#define UVRPC_ERROR_INVALID_PARAM -2
#define UVRPC_ERROR_NO_MEMORY   -3
#define UVRPC_ERROR_SERVICE_NOT_FOUND -4
#define UVRPC_ERROR_TIMEOUT     -5

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */