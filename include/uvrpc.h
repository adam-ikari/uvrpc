#ifndef UVRPC_H
#define UVRPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <uv.h>
#include <zmq.h>

/* 错误码定义 */
#define UVRPC_OK                0
#define UVRPC_ERROR             -1
#define UVRPC_ERROR_INVALID_PARAM -2
#define UVRPC_ERROR_NO_MEMORY   -3
#define UVRPC_ERROR_SERVICE_NOT_FOUND -4
#define UVRPC_ERROR_TIMEOUT     -5
#define UVRPC_ERROR_NOT_FOUND   -6

/* RPC 模式枚举 */
typedef enum {
    UVRPC_MODE_REQ_REP = 0,        /* 请求-响应模式 (REQ/REP) - 简单 RPC */
    UVRPC_MODE_ROUTER_DEALER = 1,  /* 路由-代理模式 (ROUTER/DEALER) - 多客户端异步 RPC */
    UVRPC_MODE_PUB_SUB = 2,        /* 发布-订阅模式 (PUB/SUB) - 事件通知 */
    UVRPC_MODE_PUSH_PULL = 3       /* 管道模式 (PUSH/PULL) - 任务分发 */
} uvrpc_mode_t;

/* 前向声明 */
typedef struct uvrpc_server uvrpc_server_t;
typedef struct uvrpc_client uvrpc_client_t;

/* 服务处理函数签名
 * ctx: 用户自定义上下文
 * request_data: flatbuffers 序列化的请求数据
 * request_size: 请求数据大小
 * response_data: 输出参数，响应数据（需调用者释放）
 * response_size: 输出参数，响应数据大小
 * 返回值: UVRPC_OK 表示成功，其他值为错误码
 */
typedef int (*uvrpc_service_handler_t)(void* ctx,
                                         const uint8_t* request_data,
                                         size_t request_size,
                                         uint8_t** response_data,
                                         size_t* response_size);

/* 客户端响应回调函数签名
 * ctx: 用户自定义上下文
 * status: RPC 调用状态码
 * response_data: 响应数据（零拷贝，来自 flatbuffers）
 * response_size: 响应数据大小
 */
typedef void (*uvrpc_response_callback_t)(void* ctx,
                                            int status,
                                            const uint8_t* response_data,
                                            size_t response_size);

/* ==================== 服务端 API ==================== */

/**
 * 创建 RPC 服务器（使用模式枚举）
 * @param loop libuv 事件循环
 * @param bind_addr 绑定地址（如 "tcp://0.0.0.0:5555"）
 * @param mode RPC 模式（UVRPC_MODE_REQ_REP, UVRPC_MODE_ROUTER_DEALER 等）
 * @return 服务器实例，失败返回 NULL
 */
uvrpc_server_t* uvrpc_server_new(uv_loop_t* loop, const char* bind_addr, uvrpc_mode_t mode);

/**
 * 创建 RPC 服务器（直接指定 ZMQ socket 类型）
 * @param loop libuv 事件循环
 * @param bind_addr 绑定地址（如 "tcp://0.0.0.0:5555"）
 * @param zmq_type ZMQ socket 类型（ZMQ_REP, ZMQ_REQ, ZMQ_PUB, ZMQ_SUB, ZMQ_PUSH, ZMQ_PULL 等）
 * @return 服务器实例，失败返回 NULL
 */
uvrpc_server_t* uvrpc_server_new_zmq(uv_loop_t* loop, const char* bind_addr, int zmq_type);

/**
 * 注册服务处理器
 * @param server 服务器实例
 * @param service_name 服务名称
 * @param handler 服务处理函数
 * @param ctx 用户自定义上下文（传递给 handler）
 * @return UVRPC_OK 成功，其他值为错误码
 */
int uvrpc_server_register_service(uvrpc_server_t* server,
                                   const char* service_name,
                                   uvrpc_service_handler_t handler,
                                   void* ctx);

/**
 * 启动服务器
 * @param server 服务器实例
 * @return UVRPC_OK 成功，其他值为错误码
 */
int uvrpc_server_start(uvrpc_server_t* server);

/**
 * 停止服务器
 * @param server 服务器实例
 */
void uvrpc_server_stop(uvrpc_server_t* server);

/**
 * 释放服务器资源
 * @param server 服务器实例
 */
void uvrpc_server_free(uvrpc_server_t* server);

/* ==================== 客户端 API ==================== */

/**
 * 创建 RPC 客户端（使用模式枚举）
 * @param loop libuv 事件循环
 * @param server_addr 服务器地址（如 "tcp://127.0.0.1:5555"）
 * @param mode RPC 模式（UVRPC_MODE_REQ_REP, UVRPC_MODE_ROUTER_DEALER 等）
 * @return 客户端实例，失败返回 NULL
 */
uvrpc_client_t* uvrpc_client_new(uv_loop_t* loop, const char* server_addr, uvrpc_mode_t mode);

/**
 * 创建 RPC 客户端（直接指定 Nanomq 类型）
 * @param loop libuv 事件循环
 * @param server_addr 服务器地址（如 "tcp://127.0.0.1:5555"）
 * @param nm_type Nanomq socket 类型（NN_REQ, NN_REP, NN_PUB, NN_SUB, NN_PUSH, NN_PULL 等）
 * @return 客户端实例，失败返回 NULL
 */
uvrpc_client_t* uvrpc_client_new_nanomq(uv_loop_t* loop, const char* server_addr, int nm_type);

/**
 * 异步调用 RPC 服务
 * @param client 客户端实例
 * @param service_name 服务名称
 * @param method_name 方法名称
 * @param request_data msgpack 序列化的请求数据
 * @param request_size 请求数据大小
 * @param callback 响应回调函数
 * @param ctx 用户自定义上下文（传递给 callback）
 * @return UVRPC_OK 成功，其他值为错误码
 */
int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_name,
                       const char* method_name,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx);

/**
 * 连接到服务器
 * @param client 客户端实例
 * @return UVRPC_OK 成功，其他值为错误码
 */
int uvrpc_client_connect(uvrpc_client_t* client);

/**
 * 断开连接
 * @param client 客户端实例
 */
void uvrpc_client_disconnect(uvrpc_client_t* client);

/**
 * 释放客户端资源
 * @param client 客户端实例
 */
void uvrpc_client_free(uvrpc_client_t* client);

/* ==================== 工具函数 ==================== */

/**
 * 获取错误描述
 * @param error_code 错误码
 * @return 错误描述字符串
 */
const char* uvrpc_strerror(int error_code);

/**
 * 获取模式名称
 * @param mode RPC 模式
 * @return 模式名称字符串
 */
const char* uvrpc_mode_name(uvrpc_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */