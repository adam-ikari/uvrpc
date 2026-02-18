# UVBus 和 UVRPC 架构设计

## 设计原则

### UVRPC 核心哲学
- 极简设计：最小化 API，每个函数只做一件事
- 零线程：所有 I/O 在事件循环中异步执行
- 零锁：单线程模型，无需锁机制
- 零全局变量：所有状态通过上下文传递
- **Loop 注入**：不管理 loop 生命周期，完全由用户控制

### UVBus 定位
- **底层传输抽象**：纯粹的字节传输层
- **无业务逻辑**：不处理 RPC 协议、序列化等
- **极简接口**：connect, send, recv, disconnect
- **Loop 用户管理**：接收用户提供的 loop，不管理其生命周期

## 架构层次

```
┌─────────────────────────────────────┐
│         UVRPC (RPC 框架)            │
│  - 请求/响应管理                    │
│  - 序列化/反序列化 (FlatBuffers)    │
│  - 方法路由                         │
│  - 回调管理                         │
└──────────────┬──────────────────────┘
               │
               ↓ 字节流
┌─────────────────────────────────────┐
│         UVBus (传输层)              │
│  - TCP/UDP/IPC/INPROC 抽象          │
│  - 连接管理                         │
│  - 字节发送/接收                    │
│  - 错误处理                         │
└──────────────┬──────────────────────┘
               │
               ↓ 网络传输
┌─────────────────────────────────────┐
│         libuv (事件循环)            │
│  - 异步 I/O                          │
│  - 事件驱动                         │
└─────────────────────────────────────┘
```

## Loop 注入设计

### 核心原则

UVBus 采用 **Loop 注入** 模式，完全遵循 UVRPC 的设计哲学：

1. **不创建 Loop**：UVBus 不创建任何事件循环
2. **不启动 Loop**：UVBus 不调用 `uv_run()`
3. **不停止 Loop**：UVBus 不调用 `uv_stop()`
4. **不释放 Loop**：UVBus 不调用 `uv_loop_close()` 或 `uv_loop_delete()`

### 用户责任

用户完全控制 loop 的生命周期：

```c
/* 1. 用户创建和初始化 loop */
uv_loop_t loop;
uv_loop_init(&loop);

/* 2. 用户创建 UVBus 实例（注入 loop） */
uvbus_t* server = uvbus_server_new(&loop, UVBUS_TRANSPORT_TCP, "tcp://127.0.0.1:5555");
uvbus_server_set_recv_callback(server, on_recv, &server_ctx);
uvbus_server_listen(server);

/* 3. 用户启动 loop */
uv_run(&loop, UV_RUN_DEFAULT);

/* 4. 用户停止 loop（可选） */
uv_stop(&loop);

/* 5. 用户清理 loop */
uv_loop_close(&loop);
```

### 多实例支持

Loop 可以在多个实例间共享：

```c
/* 场景 1：多个服务器共享一个 loop */
uv_loop_t loop;
uv_loop_init(&loop);

uvbus_t* server1 = uvbus_server_new(&loop, UVBUS_TRANSPORT_TCP, "tcp://127.0.0.1:5555");
uvbus_t* server2 = uvbus_server_new(&loop, UVBUS_TRANSPORT_TCP, "tcp://127.0.0.1:5556");
uvbus_t* client = uvbus_client_new(&loop, UVBUS_TRANSPORT_TCP, "tcp://127.0.0.1:5557");

uvbus_server_listen(server1);
uvbus_server_listen(server2);
uvbus_client_connect(client);

/* 所有实例共享同一个 loop */
uv_run(&loop, UV_RUN_DEFAULT);
```

### 优势

1. **灵活性**：用户完全控制 loop 的行为
2. **可测试性**：易于单元测试和集成测试
3. **可复用性**：loop 可以在多个实例间共享
4. **云原生**：支持容器化和微服务架构
5. **资源控制**：用户决定何时创建/销毁 loop
6. **避免竞争**：用户控制 loop 的线程归属

### 注意事项

1. **Loop 生命周期**：确保在使用 UVBus 期间 loop 仍然有效
2. **线程安全**：loop 必须在其创建的线程中运行
3. **资源清理**：先释放 UVBus 实例，再清理 loop
4. **错误处理**：检查 loop 是否已初始化

### 正确的使用顺序

```c
/* 正确的顺序 */
uv_loop_t loop;
uv_loop_init(&loop);              /* 1. 初始化 loop */
uvbus_t* bus = uvbus_server_new(&loop, ...);  /* 2. 创建实例 */
uvbus_server_listen(bus);         /* 3. 启动监听 */
uv_run(&loop, UV_RUN_DEFAULT);    /* 4. 运行 loop */
uvbus_server_free(bus);           /* 5. 释放实例 */
uv_loop_close(&loop);             /* 6. 清理 loop */
```

## UVBus 接口定义

### 核心概念

```c
/* UVBus 传输句柄 */
typedef struct uvbus uvbus_t;

/* 传输类型 */
typedef enum {
    UVBUS_TRANSPORT_TCP = 0,
    UVBUS_TRANSPORT_UDP = 1,
    UVBUS_TRANSPORT_IPC = 2,
    UVBUS_TRANSPORT_INPROC = 3
} uvbus_transport_type_t;

/* 回调类型 */
typedef void (*uvbus_recv_callback_t)(const uint8_t* data, size_t size, void* ctx);
typedef void (*uvbus_connect_callback_t)(uvbus_error_t status, void* ctx);
typedef void (*uvbus_error_callback_t)(uvbus_error_t error, const char* msg, void* ctx);
```

### 服务器 API

```c
/* 创建 UVBus 服务器 */
uvbus_t* uvbus_server_new(uv_loop_t* loop, 
                           uvbus_transport_type_t transport,
                           const char* address);

/* 设置接收回调 */
void uvbus_server_set_recv_callback(uvbus_t* bus, 
                                     uvbus_recv_callback_t cb, 
                                     void* ctx);

/* 设置错误回调 */
void uvbus_server_set_error_callback(uvbus_t* bus,
                                      uvbus_error_callback_t cb,
                                      void* ctx);

/* 开始监听 */
uvbus_error_t uvbus_server_listen(uvbus_t* bus);

/* 发送数据（到所有客户端） */
uvbus_error_t uvbus_server_send(uvbus_t* bus, 
                                 const uint8_t* data, 
                                 size_t size);

/* 发送数据（到特定客户端） */
uvbus_error_t uvbus_server_send_to(uvbus_t* bus, 
                                    const uint8_t* data, 
                                    size_t size, 
                                    void* client_id);

/* 停止服务器 */
void uvbus_server_stop(uvbus_t* bus);

/* 释放服务器 */
void uvbus_server_free(uvbus_t* bus);
```

### 客户端 API

```c
/* 创建 UVBus 客户端 */
uvbus_t* uvbus_client_new(uv_loop_t* loop,
                           uvbus_transport_type_t transport,
                           const char* address);

/* 设置接收回调 */
void uvbus_client_set_recv_callback(uvbus_t* bus,
                                     uvbus_recv_callback_t cb,
                                     void* ctx);

/* 设置连接回调 */
void uvbus_client_set_connect_callback(uvbus_t* bus,
                                       uvbus_connect_callback_t cb,
                                       void* ctx);

/* 设置错误回调 */
void uvbus_client_set_error_callback(uvbus_t* bus,
                                      uvbus_error_callback_t cb,
                                      void* ctx);

/* 连接到服务器 */
uvbus_error_t uvbus_client_connect(uvbus_t* bus);

/* 发送数据到服务器 */
uvbus_error_t uvbus_client_send(uvbus_t* bus,
                                 const uint8_t* data,
                                 size_t size);

/* 断开连接 */
void uvbus_client_disconnect(uvbus_t* bus);

/* 释放客户端 */
void uvbus_client_free(uvbus_t* bus);
```

## UVRPC 对 UVBus 的使用

### UVRPC 服务器

```c
typedef struct uvrpc_server {
    uv_loop_t* loop;
    uvbus_t* uvbus;  /* 使用 UVBus 进行传输 */
    
    /* RPC 相关状态 */
    uvrpc_handler_t* handlers;
    /* ... 其他 RPC 状态 */
} uvrpc_server_t;

/* 服务器接收回调 */
static void server_on_recv(const uint8_t* data, size_t size, void* ctx) {
    uvrpc_server_t* server = (uvrpc_server_t*)ctx;
    
    /* UVRPC 处理 RPC 协议 */
    uint32_t msgid;
    char* method;
    const uint8_t* params;
    size_t params_size;
    
    if (uvrpc_decode_request(data, size, &msgid, &method, 
                             &params, &params_size) == UVRPC_OK) {
        /* 路由到处理器 */
        uvrpc_handler_t* handler = find_handler(server, method);
        if (handler) {
            handler->cb(msgid, params, params_size, handler->ctx);
        }
    }
}

/* 创建服务器 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config) {
    uvrpc_server_t* server = uvrpc_alloc(sizeof(uvrpc_server_t));
    
    /* 创建 UVBus 传输层 */
    server->uvbus = uvbus_server_new(config->loop, 
                                     config->transport,
                                     config->address);
    
    /* 设置接收回调 */
    uvbus_server_set_recv_callback(server->uvbus, server_on_recv, server);
    
    /* 启动监听 */
    uvbus_server_listen(server->uvbus);
    
    return server;
}
```

### UVRPC 客户端

```c
typedef struct uvrpc_client {
    uv_loop_t* loop;
    uvbus_t* uvbus;  /* 使用 UVBus 进行传输 */
    
    /* RPC 相关状态 */
    uint32_t next_msgid;
    /* ... 其他 RPC 状态 */
} uvrpc_client_t;

/* 客户端接收回调 */
static void client_on_recv(const uint8_t* data, size_t size, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;
    
    /* UVRPC 处理 RPC 响应 */
    uint32_t msgid;
    uvrpc_error_t error;
    const uint8_t* result;
    size_t result_size;
    
    if (uvrpc_decode_response(data, size, &msgid, &error,
                              &result, &result_size) == UVRPC_OK) {
        /* 触发响应回调 */
        uvrpc_call_t* call = find_call(client, msgid);
        if (call && call->callback) {
            call->callback(error, result, result_size, call->ctx);
        }
    }
}

/* 发送 RPC 调用 */
uvrpc_error_t uvrpc_client_call(uvrpc_client_t* client,
                                  const char* method,
                                  const uint8_t* params,
                                  size_t params_size,
                                  uvrpc_response_callback_t cb,
                                  void* ctx) {
    /* 编码请求 */
    uint8_t* data;
    size_t size;
    uint32_t msgid = client->next_msgid++;
    
    if (uvrpc_encode_request(msgid, method, params, params_size,
                            &data, &size) != UVRPC_OK) {
        return UVRPC_ERROR_ENCODE;
    }
    
    /* 通过 UVBus 发送 */
    uvbus_error_t err = uvbus_client_send(client->uvbus, data, size);
    uvrpc_free(data);
    
    return err == UVBUS_OK ? UVRPC_OK : UVRPC_ERROR_TRANSPORT;
}
```

## 设计优势

### 1. 职责清晰
- **UVBus**：只负责传输，不关心内容
- **UVRPC**：只负责 RPC 语义，不关心传输细节

### 2. 可测试性
- UVBus 可以独立测试传输层
- UVRPC 可以使用 mock UVBus 测试 RPC 逻辑

### 3. 可扩展性
- 新增传输协议只需修改 UVBus
- 新增 RPC 功能只需修改 UVRPC

### 4. 一致性
- 两者都遵循极简设计原则
- 统一的错误处理
- 统一的回调模式

## 实现要点

### UVBus 实现
1. 每种传输（TCP/UDP/IPC/INPROC）独立实现
2. 统一的接口和回调机制
3. 零全局变量（INPROC 除外，但使用互斥锁保护）
4. 内存管理清晰，无泄漏

### UVRPC 实现
1. 完全依赖 UVBus 进行传输
2. 不访问 UVBus 内部实现
3. 管理自己的请求/响应状态
4. 使用 FlatBuffers 进行序列化

## 迁移路径

1. **简化 UVBus**
   - 移除复杂的抽象
   - 提供清晰的传输层接口
   - 确保零全局变量（INPROC 使用锁）

2. **重构 UVRPC**
   - 移除旧的传输层代码
   - 完全基于新的 UVBus API
   - 确保职责分离

3. **测试验证**
   - 独立测试 UVBus 传输层
   - 集成测试 UVRPC + UVBus
   - 性能测试确保无回归

## 总结

这个设计遵循 UVRPC 的核心哲学：
- **极简**：UVBus 和 UVRPC 都有最小化 API
- **零线程**：所有操作在事件循环中
- **零锁**：单线程模型（INPROC 除外）
- **零全局变量**：所有状态通过上下文传递

清晰的职责分离使得：
- 代码更易理解和维护
- 测试更简单
- 扩展更容易
- 性能更优