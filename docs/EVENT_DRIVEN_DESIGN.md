# 事件驱动和 Loop 注入模式

## 核心原则

### 1. 完全事件驱动
- **永远不阻塞事件循环**：不使用 `uv_run`、`uv_sleep`、`usleep` 等阻塞调用
- **异步回调**：所有 I/O 操作都是异步的，通过回调通知
- **非阻塞 API**：所有 API 立即返回，不等待操作完成

### 2. Loop 注入模式
- **用户拥有 Loop**：loop 由用户创建和销毁
- **传输层不拥有 Loop**：传输层只存储 loop 指针，不管理其生命周期
- **不占用 loop->data**：loop->data 完全由用户管理，传输层不得触碰
- **支持多实例**：可以创建多个 transport 实例，共享同一个 loop

## 错误示例

### ❌ 错误 1：嵌套调用 uv_run

```c
/* WRONG: 在回调中调用 uv_run */
static void on_write_async(uv_async_t* handle) {
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)handle->data;
    uv_run(client->loop, UV_RUN_NOWAIT);  /* WRONG! */
}

/* 问题：
 * 1. 嵌套调用 uv_run 会导致事件循环混乱
 * 2. 可能导致死锁或无限递归
 * 3. 破坏了事件驱动的设计
 */
```

### ❌ 错误 2：阻塞等待

```c
/* WRONG: 阻塞等待连接完成 */
int uv_transport_connect(uv_transport_t* transport) {
    int connected = 0;
    while (!connected) {
        uv_run(transport->loop, UV_RUN_ONCE);  /* WRONG! Blocking */
    }
    return 0;
}

/* 问题：
 * 1. 阻塞了事件循环，其他事件无法处理
 * 2. 可能导致死锁
 * 3. 违反了事件驱动原则
 */
```

### ❌ 错误 3：占用 loop->data

```c
/* WRONG: 修改 loop->data */
struct uv_transport {
    void* old_loop_data;  /* Save old data */
    uv_loop_t* loop;
    /* ... */
};

int uv_transport_create(uv_transport_type_t type, uv_loop_t* loop, ...) {
    transport->old_loop_data = loop->data;  /* WRONG! */
    loop->data = transport;  /* WRONG! */
    return 0;
}

void uv_transport_destroy(uv_transport_t* transport) {
    transport->loop->data = transport->old_loop_data;  /* WRONG! */
}

/* 问题：
 * 1. 破坏了用户的数据
 * 2. 用户可能需要 loop->data 存储自己的上下文
 * 3. 多个 transport 实例会互相覆盖
 */
```

### ❌ 错误 4：在回调中阻塞

```c
/* WRONG: 在回调中执行耗时操作 */
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    /* Process data */
    process_large_data(buf->base, nread);  /* WRONG! Blocking */
    
    /* Send response synchronously */
    while (!send_complete) {
        send_data();  /* WRONG! Blocking */
    }
}

/* 问题：
 * 1. 阻塞了事件循环
 * 2. 其他连接无法处理
 * 3. 性能严重下降
 */
```

## 正确示例

### ✅ 正确 1：完全异步发送

```c
/* GOOD: 异步发送，立即返回 */
int uv_transport_send(uv_transport_t* transport, const uint8_t* data, size_t size) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    /* 分配写请求 */
    uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
    if (!req) return UV_TRANSPORT_ERROR_NO_MEMORY;
    
    /* 分配数据缓冲区 */
    uint8_t* buf_data = (uint8_t*)uvrpc_alloc(size);
    if (!buf_data) {
        uvrpc_free(req);
        return UV_TRANSPORT_ERROR_NO_MEMORY;
    }
    memcpy(buf_data, data, size);
    
    /* 设置写请求 */
    uv_buf_t buf = uv_buf_init((char*)buf_data, size);
    req->data = buf_data;
    
    /* 提交异步写请求 */
    int result = uv_write(req, (uv_stream_t*)&tcp->handle, &buf, 1, on_write);
    if (result != 0) {
        uvrpc_free(buf_data);
        uvrpc_free(req);
        return UV_TRANSPORT_ERROR_IO;
    }
    
    /* 立即返回，不等待 */
    return UV_TRANSPORT_OK;
}

/* 写完成回调 - 由 libuv 调用 */
static void on_write(uv_write_t* req, int status) {
    /* 只释放资源 */
    uvrpc_free(req->data);
    uvrpc_free(req);
    
    /* 注意：永远不调用 uv_run */
    /* 注意：永远不阻塞 */
}
```

### ✅ 正确 2：异步连接

```c
/* GOOD: 异步连接，通过回调通知 */
int uv_transport_connect(uv_transport_t* transport) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    /* 初始化 TCP handle */
    uv_tcp_init(tcp->loop, &tcp->handle);
    tcp->handle.data = tcp;
    
    /* 设置服务器地址 */
    struct sockaddr_in addr;
    uv_ip4_addr(tcp->address, tcp->port, &addr);
    
    /* 提交异步连接请求 */
    tcp->connect_req.data = transport;
    int result = uv_tcp_connect(&tcp->connect_req, &tcp->handle, 
                                (const struct sockaddr*)&addr, on_connect);
    
    /* 立即返回，不等待 */
    return result;
}

/* 连接完成回调 - 由 libuv 调用 */
static void on_connect(uv_connect_t* req, int status) {
    uv_transport_t* transport = (uv_transport_t*)req->data;
    
    /* 启动读取 */
    uv_read_start((uv_stream_t*)&transport->handle, on_alloc, on_read);
    
    /* 调用用户回调通知连接状态 */
    if (transport->config.on_connect) {
        transport->config.on_connect(transport, status, transport->config.callback_ctx);
    }
    
    /* 注意：永远不调用 uv_run */
}
```

### ✅ 正确 3：Loop 注入模式

```c
/* GOOD: 只存储 loop 指针，不拥有 loop */
struct uv_transport {
    uv_transport_type_t type;
    uv_loop_t* loop;  /* 只是一个指针，不拥有 loop */
    const char* address;
    
    /* Callbacks */
    uv_transport_config_t config;
    
    /* Transport-specific fields */
    /* ... */
};

/* 创建 transport */
uv_transport_t* uv_transport_create(uv_transport_type_t type, uv_loop_t* loop, 
                                     const uv_transport_config_t* config) {
    uv_transport_t* transport = (uv_transport_t*)uvrpc_alloc(sizeof(uv_transport_t));
    
    transport->type = type;
    transport->loop = loop;  /* 只存储指针 */
    transport->address = uvrpc_strdup(config->address);
    
    /* 复制配置 */
    transport->config = *config;
    
    /* 注意：永远不触碰 loop->data */
    /* 注意：永远不调用 uv_run */
    
    return transport;
}

/* 销毁 transport */
void uv_transport_destroy(uv_transport_t* transport) {
    /* 关闭所有 handles */
    /* 释放所有资源 */
    
    /* 注意：不释放 loop，因为不拥有它 */
    /* 注意：不恢复 loop->data，因为从未修改过 */
    
    uvrpc_free(transport);
}
```

### ✅ 正确 4：异步接收

```c
/* GOOD: 异步接收，不阻塞 */
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)stream->data;
    
    if (nread > 0) {
        /* 调用用户回调处理数据 */
        if (tcp->config.on_recv) {
            tcp->config.on_recv((uv_transport_t*)tcp, 
                               (uint8_t*)buf->base, nread, 
                               NULL,  /* peer info */
                               tcp->config.callback_ctx);
        }
    } else if (nread < 0) {
        /* 错误处理 */
        if (tcp->config.on_error) {
            tcp->config.on_error((uv_transport_t*)tcp, nread, uv_strerror(nread), 
                                tcp->config.callback_ctx);
        }
    }
    
    /* 释放缓冲区 */
    if (buf->base) {
        uvrpc_free(buf->base);
    }
    
    /* 注意：永远不调用 uv_run */
    /* 注意：永远不阻塞 */
}
```

## 多实例示例

```c
/* 用户代码：创建多个 transport 实例，共享同一个 loop */
uv_loop_t loop;
uv_loop_init(&loop);

/* 设置用户数据 */
struct user_context {
    int request_count;
    /* ... */
};
struct user_context ctx = {0};
loop.data = &ctx;  /* 用户完全控制 loop->data */

/* 创建第一个 transport */
uv_transport_config_t config1 = {
    .loop = &loop,
    .address = "tcp://127.0.0.1:7001",
    .is_server = 1,
    .on_recv = on_recv1,
    .callback_ctx = &ctx
};
uv_transport_t* transport1 = uv_transport_create(UV_TRANSPORT_TCP, &config1);
uv_transport_listen(transport1);

/* 创建第二个 transport */
uv_transport_config_t config2 = {
    .loop = &loop,
    .address = "tcp://127.0.0.1:7002",
    .is_server = 1,
    .on_recv = on_recv2,
    .callback_ctx = &ctx
};
uv_transport_t* transport2 = uv_transport_create(UV_TRANSPORT_TCP, &config2);
uv_transport_listen(transport2);

/* 运行事件循环 */
uv_run(&loop, UV_RUN_DEFAULT);

/* 清理 */
uv_transport_stop(transport1);
uv_transport_destroy(transport1);
uv_transport_stop(transport2);
uv_transport_destroy(transport2);
uv_loop_close(&loop);

/* 注意：loop->data 始终由用户控制 */
```

## 最佳实践总结

### ✅ DO（应该做的）

1. **完全异步**：所有 I/O 操作都是异步的，通过回调通知
2. **立即返回**：所有 API 立即返回，不等待操作完成
3. **只存储指针**：只存储 loop 指针，不拥有 loop
4. **调用用户回调**：在适当的时候调用用户回调通知状态变化
5. **释放资源**：在回调中释放资源，不阻塞

### ❌ DON'T（不应该做的）

1. **不调用 uv_run**：永远不在任何地方调用 `uv_run`
2. **不阻塞等待**：不使用 `uv_sleep`、`usleep` 等阻塞调用
3. **不触碰 loop->data**：永远不读取或修改 `loop->data`
4. **不阻塞回调**：不在回调中执行耗时操作
5. **不嵌套调用**：不在回调中调用可能触发其他回调的函数

## 性能优势

### 完全事件驱动的优势

1. **高并发**：可以同时处理大量连接
2. **低延迟**：没有阻塞延迟，响应更快
3. **高吞吐量**：充分利用 CPU，不浪费时间在阻塞等待上
4. **可扩展**：轻松扩展到数千个并发连接

### Loop 注入模式的优势

1. **灵活性**：用户可以控制 loop 的生命周期
2. **多实例**：可以在同一个 loop 上创建多个 transport 实例
3. **集成性**：可以轻松集成到现有的事件驱动应用中
4. **可测试性**：可以注入测试 loop 进行单元测试

## 总结

遵循**完全事件驱动**和**Loop 注入模式**是 UVRPC 设计的核心原则：

- **永远不阻塞事件循环**
- **永远不触碰 loop->data**
- **永远不调用 uv_run**
- **所有操作都是异步的**
- **用户完全控制 loop**

这样的设计确保了高性能、高并发和易于集成。