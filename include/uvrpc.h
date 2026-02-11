#ifndef UVRPC_H
#define UVRPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <uv.h>
#include <zmq.h>

/**
 * @file uvrpc.h
 * @brief uvrpc - 基于 libuv 和 ZeroMQ 的高性能 RPC 框架
 * 
 * ## 并发模型
 * 
 * uvrpc 采用**单线程事件循环模型**：
 * - 所有 I/O 操作在单个线程的 libuv 事件循环中处理
 * - 依靠 libuv 的高效事件驱动机制实现高并发
 * - 避免了锁和竞态条件，简化了编程模型
 * - 支持多实例：每个服务端/客户端可以有独立的事件循环
 * 
 * ## 线程安全
 * 
 * - **单实例安全**：单个 server/client 实例必须在同一个线程中使用
 * - **跨实例安全**：不同的 server/client 可以在不同线程中运行
 * - **禁止并发调用**：不要从多个线程同时调用同一个实例的 API
 * 
 * ## 推荐架构
 * 
 * ### 方案 1: 单线程（推荐用于简单场景）
 * - 整个应用使用一个 libuv 事件循环
 * - 所有 API 调用都在事件循环线程中
 * - 适合：低流量、单连接场景
 * 
 * ### 方案 2: 多实例（推荐用于高并发场景）
 * - **推荐模式**：每个连接使用独立的 server/client 实例
 * - 使用线程池管理多个事件循环
 * - 例如：N 个线程，每个线程运行 M 个 client 实例
 * - 总并发能力 = N × M（每个实例处理数千连接）
 * - 适合：高流量、多连接、生产环境
 * 
 * @code
 * // 多实例示例：4 个线程，每个线程 10 个 client
 * void worker_thread(void* arg) {
 *     uv_loop_t loop;
 *     uv_loop_init(&loop);
 *     
 *     // 创建 10 个 client 实例
 *     for (int i = 0; i < 10; i++) {
 *         uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://server:5555", UVRPC_MODE_REQ_REP);
 *         // 使用 client...
 *     }
 *     
 *     uv_run(&loop, UV_RUN_DEFAULT);
 * }
 * @endcode
 * 
 * ## 性能特性
 * 
 * - **零拷贝 I/O**：ZMQ 和 libuv 支持零拷贝，减少内存复制
 * - **批量处理**：uvzmq 批量处理消息（最多 1000 条/批次）
 * - **事件驱动**：使用 uv_poll 替代轮询，性能提升 200 倍
 * - **可扩展**：通过多实例线性扩展吞吐量
 * 
 * ## 内存管理
 * 
 * - 使用统一的分配器宏（UVRPC_MALLOC/UVRPC_FREE）
 * - 支持自定义内存分配器（如 mimalloc）
 * - 调用者负责释放 API 返回的内存
 * 
 * ## 分配器选择
 * 
 * - **默认**：系统 malloc/free（无需额外依赖）
 * - **推荐**：mimalloc（高性能、低碎片）
 * - **自定义**：支持自定义分配器
 * 
 * 编译选项：
 * - CMake: `-DUVRPC_USE_MIMALLOC=ON` 启用 mimalloc
 * - Makefile: `UVRPC_ALLOCATOR=mimalloc` 启用 mimalloc
 * - 自定义：定义 `UVRPC_USE_CUSTOM_ALLOCATOR` 并实现 `UVRPC_CUSTOM_MALLOC/FREE`
 */

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

/**
 * 获取客户端的底层 ZMQ socket（高级用法）
 * 
 * @warning 此函数仅用于高级用例（如性能测试）。直接操作 ZMQ socket 可能导致意外行为。
 * 
 * @param client 客户端实例
 * @return ZMQ socket 指针，如果 client 无效则返回 NULL
 */
void* uvrpc_client_get_zmq_socket(uvrpc_client_t* client);

/* ==================== 异步调用 API (类似 JS await) ==================== */

/**
 * @defgroup uvrpc_async 异步调用 API
 * @{
 */

/* 异步调用状态 */
typedef enum {
    UVRPC_ASYNC_PENDING = 0,    /* 等待中 */
    UVRPC_ASYNC_DONE,           /* 已完成 */
    UVRPC_ASYNC_ERROR,          /* 出错 */
    UVRPC_ASYNC_CHECKED         /* 已检查（内部使用） */
} uvrpc_async_state_t;

/* 异步调用结果 */
typedef struct uvrpc_async_result {
    int status;                 /* 状态码: UVRPC_OK 或错误码 */
    const uint8_t* response_data; /* 响应数据（零拷贝，不要释放） */
    size_t response_size;       /* 响应数据大小 */
} uvrpc_async_result_t;

/* 异步调用上下文（内部使用） */
typedef struct uvrpc_async {
    volatile int state;                  /* IDLE, PENDING, DONE */
    uvrpc_async_result_t result;         /* 结果 */
    uv_loop_t* loop;                     /* 事件循环 */
} uvrpc_async_t;

/**
 * 创建异步调用上下文
 * @param loop libuv 事件循环
 * @return 异步上下文，失败返回 NULL
 */
uvrpc_async_t* uvrpc_async_new(uv_loop_t* loop);

/**
 * 释放异步调用上下文
 * @param async 异步上下文
 */
void uvrpc_async_free(uvrpc_async_t* async);

/**
 * 异步调用 RPC 服务（await 风格）
 * 
 * @note 这个 API 使用宏 UVRPC_AWAIT 来简化使用
 * 
 * @code
 * // C99 兼容的 await 风格使用
 * UVRPC_AWAIT(result, async, client, "service.Method", req_data, req_size);
 * if (result.status == UVRPC_OK) {
 *     // 处理 result.response_data
 * }
 * @endcode
 * 
 * @param client 客户端实例
 * @param service_name 服务名称
 * @param method_name 方法名称
 * @param request_data 请求数据
 * @param request_size 请求数据大小
 * @param async 异步上下文（用于 UVRPC_AWAIT）
 * @return UVRPC_OK 成功发起调用，其他值为错误码
 */
int uvrpc_client_call_async(uvrpc_client_t* client,
                             const char* service_name,
                             const char* method_name,
                             const uint8_t* request_data,
                             size_t request_size,
                             uvrpc_async_t* async);

/**
 * 等待异步调用完成（内部使用）
 * @param async 异步上下文
 * @return 结果指针
 */
const uvrpc_async_result_t* uvrpc_await(uvrpc_async_t* async);

/**
 * 等待异步调用完成（带超时）
 * @param async 异步上下文
 * @param timeout_ms 超时时间（毫秒）
 * @return 结果指针，超时返回状态为 UVRPC_ERROR_TIMEOUT
 */
const uvrpc_async_result_t* uvrpc_await_timeout(uvrpc_async_t* async, uint64_t timeout_ms);

/**
 * 等待多个异步调用完成（并发 await）
 * 
 * 此函数允许同时等待多个异步请求，支持真正的并发调用。
 * 
 * 使用示例：
 * @code
 * // 创建多个 async 上下文
 * uvrpc_async_t* async1 = uvrpc_async_new(&loop);
 * uvrpc_async_t* async2 = uvrpc_async_new(&loop);
 * uvrpc_async_t* async3 = uvrpc_async_new(&loop);
 * 
 * // 发起所有请求（并发）
 * uvrpc_client_call_async(client, "service.Method1", req1, req1_size, async1);
 * uvrpc_client_call_async(client, "service.Method2", req2, req2_size, async2);
 * uvrpc_client_call_async(client, "service.Method3", req3, req3_size, async3);
 * 
 * // 等待所有请求完成
 * uvrpc_async_t* asyncs[] = {async1, async2, async3};
 * int completed = uvrpc_await_all(asyncs, 3);
 * 
 * // 处理结果
 * if (async1->result.status == UVRPC_OK) { ... }
 * if (async2->result.status == UVRPC_OK) { ... }
 * if (async3->result.status == UVRPC_OK) { ... }
 * 
 * uvrpc_async_free(async1);
 * uvrpc_async_free(async2);
 * uvrpc_async_free(async3);
 * @endcode
 * 
 * @param asyncs 异步上下文数组
 * @param count 异步上下文数量
 * @return 完成的请求数量
 */
int uvrpc_await_all(uvrpc_async_t** asyncs, int count);

/**
 * 等待任意一个异步调用完成（并发 await）
 * 
 * 此函数等待任意一个请求完成，适合"第一个响应获胜"的场景。
 * 
 * @param asyncs 异步上下文数组
 * @param count 异步上下文数量
 * @return 已完成的异步上下文索引，超时返回 -1
 */
int uvrpc_await_any(uvrpc_async_t** asyncs, int count);

/** @} */

/* ==================== Await 宏定义 (C99 兼容) ==================== */

/**
 * @defgroup uvrpc_await Await 宏
 * @{
 */

/**
 * Await 宏 - 等待异步调用完成
 * 
 * 使用示例：
 * @code
 * uvrpc_async_t* async = uvrpc_async_new(loop);
 * 
 * // 第一次调用
 * UVRPC_AWAIT(result1, async, client, "service.Method1", req1, req1_size);
 * if (result1.status == UVRPC_OK) {
 *     // 处理结果1
 * }
 * 
 * // 第二次调用（使用同一个 async 上下文）
 * UVRPC_AWAIT(result2, async, client, "service.Method2", req2, req2_size);
 * if (result2.status == UVRPC_OK) {
 *     // 处理结果2
 * }
 * 
 * uvrpc_async_free(async);
 * @endcode
 */
#define UVRPC_AWAIT(result_var, async, client, service, method, req_data, req_size) \
    do { \
        int _await_ret = uvrpc_client_call_async((client), (service), (method), \
                                                  (req_data), (req_size), (async)); \
        if (_await_ret != UVRPC_OK) { \
            (result_var).status = _await_ret; \
            (result_var).response_data = NULL; \
            (result_var).response_size = 0; \
            break; \
        } \
        const uvrpc_async_result_t* _await_result = uvrpc_await((async)); \
        (result_var) = *_await_result; \
    } while(0)

/**
 * 链式调用宏 - 支持类似 JS Promise 的链式调用
 * 
 * 使用示例：
 * @code
 * UVRPC_CHAIN(async, client)
 *     .THEN("service.Method1", req1, req1_size) {
 *         // 处理 result1
 *         if (result->status == UVRPC_OK) {
 *             // 准备第二次请求
 *         }
 *     }
 *     .THEN("service.Method2", req2, req2_size) {
 *         // 处理 result2
 *     }
 *     .ELSE {
 *         // 处理错误
 *     }
 *     .DONE();
 * @endcode
 */
#define UVRPC_CHAIN(async, client) \
    uvrpc_chain_t _chain = { (async), (client) }

/* 链式调用结构（内部使用） */
typedef struct {
    uvrpc_async_t* async;
    uvrpc_client_t* client;
    const char* last_error;
} uvrpc_chain_t;

/**
 * 并发发起多个请求，然后等待所有响应
 * 
 * 使用示例：
 * @code
 * // 定义请求数组
 * const char* services[] = {"service.Method1", "service.Method2", "service.Method3"};
 * const uint8_t* req_data[] = {data1, data2, data3};
 * size_t req_size[] = {size1, size2, size3};
 * 
 * uvrpc_async_result_t results[3];
 * 
 * // 并发发起请求并等待所有响应
 * UVRPC_AWAIT_ALL(results, asyncs, 3, client, services, req_data, req_size);
 * 
 * // 处理结果
 * for (int i = 0; i < 3; i++) {
 *     if (results[i].status == UVRPC_OK) {
 *         // 处理响应
 *     }
 * }
 * 
 * // 释放 async 上下文
 * for (int i = 0; i < 3; i++) {
 *     uvrpc_async_free(asyncs[i]);
 * }
 * @endcode
 */
#define UVRPC_AWAIT_ALL(results, asyncs, count, client, services, methods, req_data, req_size) \
    do { \
        for (int i = 0; i < (count); i++) { \
            (asyncs)[i] = uvrpc_async_new((client)->loop); \
            if (!(asyncs)[i]) { \
                (results)[i].status = UVRPC_ERROR_NO_MEMORY; \
                (results)[i].response_data = NULL; \
                (results)[i].response_size = 0; \
                continue; \
            } \
            int _ret = uvrpc_client_call_async((client), (services)[i], (methods)[i], \
                                                 (req_data)[i], (req_size)[i], (asyncs)[i]); \
            if (_ret != UVRPC_OK) { \
                (results)[i].status = _ret; \
                (results)[i].response_data = NULL; \
                (results)[i].response_size = 0; \
            } \
        } \
        uvrpc_await_all((asyncs), (count)); \
        for (int i = 0; i < (count); i++) { \
            if ((asyncs)[i]) { \
                (results)[i] = (asyncs)[i]->result; \
            } \
        } \
    } while(0)

/** @} */

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

/**
 * 自适应事件循环运行 - 平衡性能和功耗
 * 
 * 这个函数实现了一个智能的事件循环调度策略：
 * - 高负载时：使用 UV_RUN_ONCE 模式，快速处理事件，保持高性能
 * - 低负载时：使用 UV_RUN_NOWAIT + 短暂休眠，降低 CPU 占用，节省功耗
 * - 自动监测负载状态，动态调整运行模式
 * 
 * @param loop libuv 事件循环
 * @param timeout_ms 超时时间（毫秒），0 表示无限期运行
 * @param check_fn 可选的检查函数，返回非 0 时退出循环
 * @param user_ctx 传递给检查函数的用户上下文
 * @return 0 表示正常退出，-1 表示超时
 * 
 * @code
 * // 示例：运行事件循环，直到所有请求完成
 * int should_stop(void* ctx) {
 *     return (completed_requests == total_requests) ? 1 : 0;
 * }
 * 
 * uvrpc_loop_run_adaptive(&loop, 0, should_stop, NULL);
 * @endcode
 */
int uvrpc_loop_run_adaptive(uv_loop_t* loop, int timeout_ms, 
                             int (*check_fn)(void*), void* user_ctx);

#ifdef __cplusplus
}
#endif

#endif /* UVRPC_H */