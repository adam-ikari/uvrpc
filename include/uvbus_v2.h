/**
 * UVBus v2 - 极简传输抽象层
 * 
 * 设计原则：
 * - 极简：最小化 API，每个函数只做一件事
 * - 零线程：所有操作在事件循环中异步执行
 * - 零锁：单线程模型（INPROC 除外）
 * - 零全局变量：所有状态通过上下文传递
 * - Loop 注入：不管理 loop 生命周期，完全由用户控制
 * 
 * Loop 注入设计：
 * - UVBus 接收用户提供的 uv_loop_t* 指针
 * - UVBus 不会创建、启动或停止 loop
 * - UVBus 不会释放 loop
 * - 用户完全控制 loop 的生命周期
 * - 支持多实例、多 loop、复用 loop
 * 
 * 职责：
 * - 纯粹的传输层（TCP/UDP/IPC/INPROC）
 * - 字节发送/接收
 * - 连接管理
 * - 不处理业务逻辑
 */

#ifndef UVBUS_V2_H
#define UVBUS_V2_H

#include <uv.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 错误码 */
typedef enum {
    UVBUS_OK = 0,
    UVBUS_ERROR = -1,
    UVBUS_ERROR_INVALID_PARAM = -2,
    UVBUS_ERROR_NO_MEMORY = -3,
    UVBUS_ERROR_NOT_CONNECTED = -4,
    UVBUS_ERROR_TIMEOUT = -5,
    UVBUS_ERROR_IO = -6,
    UVBUS_ERROR_ALREADY_CONNECTED = -7,
    UVBUS_ERROR_NOT_FOUND = -8
} uvbus_error_t;

/* 传输类型 */
typedef enum {
    UVBUS_TRANSPORT_TCP = 0,
    UVBUS_TRANSPORT_UDP = 1,
    UVBUS_TRANSPORT_IPC = 2,
    UVBUS_TRANSPORT_INPROC = 3
} uvbus_transport_type_t;

/* 前向声明 */
typedef struct uvbus uvbus_t;

/* 回调类型 */
typedef void (*uvbus_recv_callback_t)(const uint8_t* data, size_t size, void* ctx);
typedef void (*uvbus_connect_callback_t)(uvbus_error_t status, void* ctx);
typedef void (*uvbus_error_callback_t)(uvbus_error_t error, const char* msg, void* ctx);

/* ============================================================================
 * 服务器 API
 * ============================================================================ */

/**
 * 创建 UVBus 服务器
 * @param loop libuv 事件循环（用户拥有，不管理生命周期）
 * @param transport 传输类型
 * @param address 监听地址（如 "tcp://127.0.0.1:5555"）
 * @return 服务器实例，失败返回 NULL
 * 
 * 注意：
 * - loop 必须由用户提供，UVBus 不创建或释放 loop
 * - loop 可以在多个 UVBus 实例间共享
 * - 用户负责 loop 的初始化和运行
 * - 用户负责 loop 的清理和释放
 */
uvbus_t* uvbus_server_new(uv_loop_t* loop, 
                           uvbus_transport_type_t transport,
                           const char* address);

/**
 * 设置接收回调
 * @param bus 服务器实例
 * @param cb 接收回调
 * @param ctx 回调上下文
 */
void uvbus_server_set_recv_callback(uvbus_t* bus, 
                                     uvbus_recv_callback_t cb, 
                                     void* ctx);

/**
 * 设置错误回调
 * @param bus 服务器实例
 * @param cb 错误回调
 * @param ctx 回调上下文
 */
void uvbus_server_set_error_callback(uvbus_t* bus,
                                      uvbus_error_callback_t cb,
                                      void* ctx);

/**
 * 开始监听
 * @param bus 服务器实例
 * @return UVBUS_OK 成功，其他值失败
 */
uvbus_error_t uvbus_server_listen(uvbus_t* bus);

/**
 * 发送数据（广播到所有客户端）
 * @param bus 服务器实例
 * @param data 数据
 * @param size 数据大小
 * @return UVBUS_OK 成功，其他值失败
 */
uvbus_error_t uvbus_server_send(uvbus_t* bus, 
                                 const uint8_t* data, 
                                 size_t size);

/**
 * 发送数据（到特定客户端）
 * @param bus 服务器实例
 * @param data 数据
 * @param size 数据大小
 * @param client_id 客户端 ID（来自接收回调）
 * @return UVBUS_OK 成功，其他值失败
 */
uvbus_error_t uvbus_server_send_to(uvbus_t* bus, 
                                    const uint8_t* data, 
                                    size_t size, 
                                    void* client_id);

/**
 * 停止服务器
 * @param bus 服务器实例
 */
void uvbus_server_stop(uvbus_t* bus);

/**
 * 释放服务器
 * @param bus 服务器实例
 */
void uvbus_server_free(uvbus_t* bus);

/* ============================================================================
 * 客户端 API
 * ============================================================================ */

/**
 * 创建 UVBus 客户端
 * @param loop libuv 事件循环（用户拥有，不管理生命周期）
 * @param transport 传输类型
 * @param address 服务器地址（如 "tcp://127.0.0.1:5555"）
 * @return 客户端实例，失败返回 NULL
 * 
 * 注意：
 * - loop 必须由用户提供，UVBus 不创建或释放 loop
 * - loop 可以在多个 UVBus 实例间共享
 * - 用户负责 loop 的初始化和运行
 * - 用户负责 loop 的清理和释放
 */
uvbus_t* uvbus_client_new(uv_loop_t* loop,
                           uvbus_transport_type_t transport,
                           const char* address);

/**
 * 设置接收回调
 * @param bus 客户端实例
 * @param cb 接收回调
 * @param ctx 回调上下文
 */
void uvbus_client_set_recv_callback(uvbus_t* bus,
                                     uvbus_recv_callback_t cb,
                                     void* ctx);

/**
 * 设置连接回调
 * @param bus 客户端实例
 * @param cb 连接回调
 * @param ctx 回调上下文
 */
void uvbus_client_set_connect_callback(uvbus_t* bus,
                                       uvbus_connect_callback_t cb,
                                       void* ctx);

/**
 * 设置错误回调
 * @param bus 客户端实例
 * @param cb 错误回调
 * @param ctx 回调上下文
 */
void uvbus_client_set_error_callback(uvbus_t* bus,
                                      uvbus_error_callback_t cb,
                                      void* ctx);

/**
 * 连接到服务器
 * @param bus 客户端实例
 * @return UVBUS_OK 成功，其他值失败
 */
uvbus_error_t uvbus_client_connect(uvbus_t* bus);

/**
 * 发送数据到服务器
 * @param bus 客户端实例
 * @param data 数据
 * @param size 数据大小
 * @return UVBUS_OK 成功，其他值失败
 */
uvbus_error_t uvbus_client_send(uvbus_t* bus,
                                 const uint8_t* data,
                                 size_t size);

/**
 * 断开连接
 * @param bus 客户端实例
 */
void uvbus_client_disconnect(uvbus_t* bus);

/**
 * 释放客户端
 * @param bus 客户端实例
 */
void uvbus_client_free(uvbus_t* bus);

/* ============================================================================
 * 辅助 API
 * ============================================================================ */

/**
 * 获取事件循环
 * @param bus 实例
 * @return libuv 事件循环
 */
uv_loop_t* uvbus_get_loop(uvbus_t* bus);

/**
 * 获取传输类型
 * @param bus 实例
 * @return 传输类型
 */
uvbus_transport_type_t uvbus_get_transport_type(uvbus_t* bus);

/**
 * 获取地址
 * @param bus 实例
 * @return 地址字符串
 */
const char* uvbus_get_address(uvbus_t* bus);

/**
 * 检查是否已连接
 * @param bus 实例
 * @return 1 已连接，0 未连接
 */
int uvbus_is_connected(uvbus_t* bus);

/**
 * 检查是否为服务器
 * @param bus 实例
 * @return 1 是服务器，0 是客户端
 */
int uvbus_is_server(uvbus_t* bus);

/**
 * 错误码转字符串
 * @param error 错误码
 * @return 错误描述
 */
const char* uvbus_strerror(uvbus_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* UVBUS_V2_H */