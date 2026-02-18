# UVRPC 与 UVBus 集成架构设计 V2

## 问题分析

### 当前问题
1. **回调签名不匹配**：`uvbus_recv_callback_t` 只传递 `(data, size, ctx)`，没有客户端标识符
2. **无法发送响应**：服务器无法知道哪个客户端发送了请求，无法发送响应
3. **架构不一致**：UVRPC 需要请求-响应模式，但 UVBus 设计为单向消息传递

### 设计约束
1. **单线程事件循环**：不能使用多线程
2. **零全局变量**：不能使用全局状态
3. **零锁**：不能使用互斥锁
4. **性能优先**：最小化开销

## 解决方案

### 方案 1：修改 UVBus 回调签名（推荐）

**优点**：
- 明确的客户端上下文
- 服务器可以立即发送响应
- 符合请求-响应语义

**缺点**：
- 需要修改 UVBus 核心 API
- 增加回调复杂度

**实现**：

```c
/* UVBus 回调签名 */
typedef void (*uvbus_recv_callback_t)(
    const uint8_t* data, 
    size_t size, 
    void* client_ctx,  /* 客户端上下文 */
    void* server_ctx   /* 服务器上下文 */
);

/* UVRPC 服务器回调 */
static void server_recv_callback(
    const uint8_t* data, 
    size_t size, 
    void* client_ctx,  /* uvbus_tcp_client_t* */
    void* server_ctx   /* uvrpc_server_t* */
) {
    uvrpc_server_t* server = (uvrpc_server_t*)server_ctx;
    void* client = client_ctx;  /* 存储客户端上下文 */

    /* 解码请求 */
    uint32_t msgid;
    char* method;
    const uint8_t* params;
    size_t params_size;

    if (uvrpc_decode_request(data, size, &msgid, &method, 
                             &params, &params_size) != UVRPC_OK) {
        return;
    }

    /* 找到 handler */
    handler_entry_t* entry = find_handler(server, method);

    if (!entry) {
        fprintf(stderr, "Handler not found: %s\n", method);
        uvrpc_free(method);
        return;
    }

    /* 创建请求对象（包含客户端上下文） */
    uvrpc_request_t req = {
        .server = server,
        .msgid = msgid,
        .method = method,
        .params = params,
        .params_size = params_size,
        .client_ctx = client,  /* 客户端上下文 */
        .user_data = NULL
    };

    /* 执行 handler */
    entry->handler(&req, entry->ctx);

    uvrpc_free(method);
}

/* 发送响应 */
int uvrpc_response_send(uvrpc_request_t* req, 
                        const uint8_t* result, 
                        size_t result_size) {
    if (!req || !req->server || !req->client_ctx) {
        return UVRPC_ERROR_INVALID_PARAM;
    }

    /* 编码响应 */
    uint8_t* response_data;
    size_t response_size;

    if (uvrpc_encode_response(req->msgid, result, result_size,
                              &response_data, &response_size) != UVRPC_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    /* 通过 UVBus 发送响应到特定客户端 */
    uvbus_t* uvbus = req->server->uvbus;
    uvbus_error_t err = uvbus_send_to(uvbus, response_data, 
                                       response_size, req->client_ctx);

    uvrpc_free(response_data);

    if (err != UVBUS_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }

    return UVRPC_OK;
}
```

### 方案 2：UVBus 维护请求映射

**优点**：
- 不改变 UVBus 核心 API
- UVBus 内部处理请求-响应匹配

**缺点**：
- UVBus 需要知道 msgid 语义
- 增加复杂性
- 不符合关注点分离原则

### 方案 3：每个客户端独立的 UVRPC 实例

**优点**：
- 完全隔离
- 简单明了

**缺点**：
- 无法共享 handler
- 资源消耗大
- 不符合单一服务器模式

## 推荐实现：方案 1

### 需要修改的文件

1. **include/uvbus.h**
   - 修改 `uvbus_recv_callback_t` 签名
   - 添加 `uvbus_send_to()` 函数

2. **src/uvbus.c**
   - 更新回调调用方式

3. **src/uvbus_transport_*.c**
   - 在接收数据时传递客户端上下文
   - 实现 `send_to()` 函数

4. **include/uvrpc.h**
   - 修改 `uvrpc_request_t` 结构
   - 添加 `uvrpc_response_send()` 函数

5. **src/uvrpc_server.c**
   - 更新 `server_recv_callback` 实现
   - 实现 `uvrpc_response_send()`

### 实现步骤

1. **修改 UVBus 回调签名**
   ```c
   typedef void (*uvbus_recv_callback_t)(
       const uint8_t* data, 
       size_t size, 
       void* client_ctx,
       void* server_ctx
   );
   ```

2. **实现 `uvbus_send_to()`**
   ```c
   uvbus_error_t uvbus_send_to(uvbus_t* bus, 
                                const uint8_t* data, 
                                size_t size, 
                                void* client_ctx);
   ```

3. **更新所有传输层实现**
   - TCP: 传递 `uvbus_tcp_client_t*`
   - UDP: 传递 `sockaddr*`
   - IPC: 传递 `uv_pipe_t*`
   - INPROC: 传递客户端标识符

4. **更新 UVRPC 服务器**
   - 修改 `uvrpc_request_t` 结构
   - 实现 `uvrpc_response_send()`

## 测试计划

1. **TCP 传输测试**
   - 多客户端并发请求
   - 响应正确路由

2. **IPC 传输测试**
   - 进程间通信
   - 请求-响应正确匹配

3. **INPROC 传输测试**
   - 进程内通信
   - 性能测试

4. **错误处理测试**
   - 客户端断开连接
   - 超时处理