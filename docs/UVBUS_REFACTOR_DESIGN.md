# UVBus 抽象层次重新设计

## 当前问题

1. **抽象层次混乱**：uvbus 同时承担了传输层抽象和 RPC 层抽象的职责
2. **职责不清**：uvbus 既管理连接，又管理消息格式（帧格式）
3. **过度设计**：虚拟函数表、union 类型等增加了复杂度
4. **难以维护**：每个传输层需要实现 7 个函数（listen, connect, disconnect, send, send_to, broadcast, free）
5. **违反事件驱动原则**：在某些地方嵌套调用 `uv_run`，阻塞事件循环
6. **Loop 所有权混乱**：传输层可能占用或修改 `loop->data`

## 重新设计原则

### 1. 单一职责原则
- **传输层**：只负责字节流的发送和接收
- **帧层**：负责消息的帧格式（长度前缀）
- **RPC 层**：负责 RPC 请求/响应的编码和解码

### 2. 简化接口
- **传输层 API**：listen, connect, send, recv, close
- **统一接口**：所有传输层使用相同的 API
- **移除 broadcast**：由应用层实现多播

### 3. 零依赖
- 传输层不依赖 RPC 层
- 可以独立使用传输层
- 可以轻松添加新的传输层

### 4. **完全事件驱动（Critical）**
- **永远不阻塞事件循环**：不使用 `uv_run`、`uv_sleep` 等阻塞调用
- **异步回调**：所有 I/O 操作都是异步的，通过回调通知
- **不管理 Loop**：传输层不管理 libuv loop 的生命周期
- **只使用 Loop**：传输层只使用用户提供的 loop，不占用 `loop->data`

### 5. **Loop 注入模式（Critical）**
- **用户拥有 Loop**：loop 由用户创建和销毁
- **传输层不拥有 Loop**：传输层只存储 loop 指针，不管理其生命周期
- **不占用 loop->data**：loop->data 完全由用户管理，传输层不得触碰
- **支持多实例**：可以创建多个 transport 实例，共享同一个 loop

## 新的抽象层次

```
┌─────────────────────────────────────────┐
│           UVRPC Layer                   │
│  (Request/Response encoding/decoding)   │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│           Frame Layer                   │
│  (4-byte length prefix + payload)       │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│         Transport Layer                │
│  (Byte stream send/recv)                │
├───────────────────┼─────────────────────┤
│ TCP    │ UDP    │ IPC    │ INPROC      │
└───────────────────┴─────────────────────┘
```

## 新的 API 设计

### 1. 传输层接口（uv_transport.h）

```c
/**
 * @brief Transport handle - opaque type
 */
typedef struct uv_transport uv_transport_t;

/**
 * @brief Transport configuration
 */
typedef struct {
    uv_loop_t* loop;
    const char* address;
    int is_server;
    
    /* Callbacks */
    void (*on_recv)(uv_transport_t* transport, const uint8_t* data, size_t size, void* ctx);
    void (*on_connect)(uv_transport_t* transport, void* ctx);
    void (*on_disconnect)(uv_transport_t* transport, void* ctx);
    void (*on_error)(uv_transport_t* transport, int error, const char* msg, void* ctx);
    void* callback_ctx;
} uv_transport_config_t;

/**
 * @brief Transport API - all transports use the same interface
 */

/* Create/destroy */
uv_transport_t* uv_transport_create(uv_transport_type_t type, const uv_transport_config_t* config);
void uv_transport_destroy(uv_transport_t* transport);

/* Server operations */
int uv_transport_listen(uv_transport_t* transport);
void uv_transport_stop(uv_transport_t* transport);

/* Client operations */
int uv_transport_connect(uv_transport_t* transport);
void uv_transport_disconnect(uv_transport_t* transport);

/* Send/recv */
int uv_transport_send(uv_transport_t* transport, const uint8_t* data, size_t size);
int uv_transport_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer);

/* Status */
int uv_transport_is_connected(uv_transport_t* transport);
```

### 2. 帧层接口（uv_frame.h）

```c
/**
 * @brief Frame handle - adds 4-byte length prefix
 */
typedef struct {
    uint8_t* data;
    size_t size;
} uv_frame_t;

/**
 * @brief Frame operations
 */

/* Encode: add 4-byte length prefix */
uv_frame_t* uv_frame_encode(const uint8_t* payload, size_t payload_size);
void uv_frame_free(uv_frame_t* frame);

/* Decode: extract payload from frame */
int uv_frame_decode(const uint8_t* frame_data, size_t frame_size, 
                   uint8_t** payload, size_t* payload_size);
```

### 3. RPC 层接口（保持现有的 uvrpc.h）

RPC 层使用传输层和帧层：
- 使用 `uv_transport_send` 发送编码后的 RPC 请求
- 在 `on_recv` 回调中使用 `uv_frame_decode` 解析帧
- 然后使用 `uvrpc_decode_request` 解析 RPC 请求

## 实现细节

### 1. TCP 传输实现

```c
struct uv_transport_tcp {
    uv_transport_t base;  /* Base transport structure */
    
    /* TCP-specific fields */
    uv_tcp_t handle;
    uv_connect_t connect_req;
    
    /* For server */
    uv_tcp_t listen_handle;
    uv_transport_tcp_client_t* clients;
    int client_count;
    
    /* Buffer */
    uint8_t read_buffer[65536];
    size_t read_pos;
    
    /* CRITICAL: Never store user data in loop->data */
    /* CRITICAL: Never call uv_run in callbacks */
    /* CRITICAL: Never block the event loop */
};

/* Only implement 5 functions */
static int tcp_send(uv_transport_t* transport, const uint8_t* data, size_t size);
static int tcp_listen(uv_transport_t* transport);
static int tcp_connect(uv_transport_t* transport);
static void tcp_close(uv_transport_t* transport);
static void tcp_free(uv_transport_t* transport);

/* Example: send implementation - fully async, never blocks */
static int tcp_send(uv_transport_t* transport, const uint8_t* data, size_t size) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)transport;
    
    /* Allocate write request */
    uv_write_t* req = (uv_write_t*)uvrpc_alloc(sizeof(uv_write_t));
    if (!req) return UV_TRANSPORT_ERROR_NO_MEMORY;
    
    /* Allocate data buffer */
    uint8_t* buf_data = (uint8_t*)uvrpc_alloc(size);
    if (!buf_data) {
        uvrpc_free(req);
        return UV_TRANSPORT_ERROR_NO_MEMORY;
    }
    memcpy(buf_data, data, size);
    
    /* Set up write request */
    uv_buf_t buf = uv_buf_init((char*)buf_data, size);
    req->data = buf_data;  /* Will be freed in write callback */
    
    /* Submit write request - fully async */
    int result = uv_write(req, (uv_stream_t*)&tcp->handle, &buf, 1, on_write);
    if (result != 0) {
        uvrpc_free(buf_data);
        uvrpc_free(req);
        
        /* Return error to caller, let them decide what to do */
        return UV_TRANSPORT_ERROR_IO;
    }
    
    return UV_TRANSPORT_OK;
}

/* Write callback - called by libuv when write completes */
static void on_write(uv_write_t* req, int status) {
    /* CRITICAL: Never call uv_run here */
    /* CRITICAL: Never block here */
    /* Just free resources and notify if needed */
    
    uvrpc_free(req->data);  /* Free data buffer */
    uvrpc_free(req);        /* Free request */
    
    /* If user needs notification, call their callback here */
    /* But never call uv_run or block */
}

/* Example: recv implementation - fully event-driven */
static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    uv_transport_tcp_t* tcp = (uv_transport_tcp_t*)stream->data;
    
    if (nread > 0) {
        /* CRITICAL: Just call user callback, never call uv_run */
        if (tcp->config.on_recv) {
            tcp->config.on_recv((uv_transport_t*)tcp, (uint8_t*)buf->base, nread, NULL, tcp->config.callback_ctx);
        }
    }
    
    /* Free buffer */
    if (buf->base) {
        uvrpc_free(buf->base);
    }
}
```

### 2. UDP 传输实现

```c
struct uv_transport_udp {
    uv_transport_t base;
    
    /* UDP-specific fields */
    uv_udp_t handle;
    struct sockaddr_in addr;
    
    /* For server */
    int is_multicast;
};

/* Same 5 functions */
static int udp_send(uv_transport_t* transport, const uint8_t* data, size_t size);
static int udp_send_to(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer);
static int udp_listen(uv_transport_t* transport);
static int udp_connect(uv_transport_t* transport);
static void udp_close(uv_transport_t* transport);
static void udp_free(uv_transport_t* transport);
```

### 3. 帧层实现

```c
/* Encode: big-endian 4-byte length prefix */
uv_frame_t* uv_frame_encode(const uint8_t* payload, size_t payload_size) {
    size_t frame_size = 4 + payload_size;
    uint8_t* frame_data = malloc(frame_size);
    
    /* Write length in big-endian */
    frame_data[0] = (payload_size >> 24) & 0xFF;
    frame_data[1] = (payload_size >> 16) & 0xFF;
    frame_data[2] = (payload_size >> 8) & 0xFF;
    frame_data[3] = payload_size & 0xFF;
    
    /* Copy payload */
    memcpy(frame_data + 4, payload, payload_size);
    
    uv_frame_t* frame = malloc(sizeof(uv_frame_t));
    frame->data = frame_data;
    frame->size = frame_size;
    
    return frame;
}

/* Decode: extract payload */
int uv_frame_decode(const uint8_t* frame_data, size_t frame_size,
                   uint8_t** payload, size_t* payload_size) {
    if (frame_size < 4) return -1;
    
    /* Parse length */
    size_t size = (frame_data[0] << 24) | (frame_data[1] << 16) | 
                  (frame_data[2] << 8) | frame_data[3];
    
    if (frame_size != 4 + size) return -1;
    
    *payload_size = size;
    *payload = frame_data + 4;  /* Pointer arithmetic, no copy */
    
    return 0;
}
```

## 迁移路径

### 阶段 1：实现新的传输层（不影响现有代码）
1. 创建 `uv_transport.h` 和 `uv_transport.c`
2. 实现 TCP 传输层
3. 编写单元测试

### 阶段 2：实现帧层
1. 创建 `uv_frame.h` 和 `uv_frame.c`
2. 编写单元测试

### 阶段 3：修改 UVRPC 使用新接口
1. 修改 `uvrpc_server.c` 使用新的传输层
2. 修改 `uvrpc_client.c` 使用新的传输层
3. 集成测试

### 阶段 4：清理旧代码
1. 删除旧的 `uvbus.c`
2. 删除旧的传输层实现
3. 更新文档

## 优势

1. **清晰的职责分离**：传输层只负责字节流，帧层负责消息格式，RPC 层负责业务逻辑
2. **易于测试**：每层可以独立测试
3. **易于扩展**：添加新的传输层只需实现 5 个函数
4. **零依赖**：传输层不依赖 RPC 层，可以单独使用
5. **简化实现**：移除了虚拟函数表、union 等复杂机制
6. **完全事件驱动**：永远不阻塞事件循环，所有操作都是异步的
7. **Loop 注入模式**：用户完全控制 loop，传输层不触碰 `loop->data`
8. **支持多实例**：可以创建多个 transport 实例，共享同一个 loop

## 事件驱动最佳实践

### ❌ 错误示例

```c
/* BAD: 嵌套调用 uv_run */
static void on_write_async(uv_async_t* handle) {
    uvbus_tcp_client_t* client = (uvbus_tcp_client_t*)handle->data;
    uv_run(client->loop, UV_RUN_NOWAIT);  /* WRONG! Blocks event loop */
}

/* BAD: 阻塞等待 */
int uv_transport_connect(uv_transport_t* transport) {
    while (!connected) {
        uv_run(transport->loop, UV_RUN_ONCE);  /* WRONG! Blocking */
    }
}

/* BAD: 占用 loop->data */
struct uv_transport {
    void* old_loop_data;  /* Save old data */
    uv_loop_t* loop;
    /* ... */
};

int uv_transport_create(...) {
    transport->old_loop_data = transport->loop->data;  /* WRONG! Touching loop->data */
    transport->loop->data = transport;
}
```

### ✅ 正确示例

```c
/* GOOD: 完全异步，不阻塞 */
static int tcp_send(uv_transport_t* transport, const uint8_t* data, size_t size) {
    uv_write_t* req = ...;
    uv_write(req, stream, &buf, 1, on_write);  /* Submit async write */
    return UV_TRANSPORT_OK;  /* Return immediately, don't wait */
}

/* GOOD: 异步通知 */
static void on_write(uv_write_t* req, int status) {
    /* Just free resources and optionally call user callback */
    free(req->data);
    free(req);
    
    /* NEVER call uv_run here */
    /* NEVER block here */
}

/* GOOD: Loop 注入模式 */
struct uv_transport {
    uv_loop_t* loop;  /* Just a pointer, don't own it */
    /* ... */
};

int uv_transport_create(uv_transport_type_t type, uv_loop_t* loop, ...) {
    transport->loop = loop;  /* Just store pointer */
    /* NEVER touch loop->data */
    /* NEVER call uv_run */
    return 0;
}
```

## 总结

新的设计遵循"少即是多"和"完全事件驱动"的原则：
- **更少的接口**：从 7 个函数减少到 5 个
- **更少的依赖**：传输层不依赖 RPC 层
- **更少的代码**：移除了虚拟函数表、union 等机制
- **更清晰的结构**：三层架构，职责明确
- **完全事件驱动**：永远不阻塞事件循环
- **Loop 注入模式**：用户完全控制 loop，不触碰 `loop->data`

### 使用新的传输层

```c
/* Create transport */
uv_transport_config_t config = {
    .loop = &loop,
    .address = "tcp://127.0.0.1:7003",
    .is_server = 0,
    .on_recv = on_recv_callback,
    .on_connect = on_connect_callback,
    .callback_ctx = &context
};

uv_transport_t* transport = uv_transport_create(UV_TRANSPORT_TCP, &config);

/* Connect */
uv_transport_connect(transport);

/* Send data */
uv_transport_send(transport, data, size);

/* Destroy */
uv_transport_destroy(transport);
```

### 使用帧层

```c
/* Encode RPC request as frame */
uv_frame_t* frame = uv_frame_encode(rpc_request_data, rpc_request_size);
uv_transport_send(transport, frame->data, frame->size);
uv_frame_free(frame);

/* Decode received frame */
uint8_t* payload;
size_t payload_size;
if (uv_frame_decode(received_data, received_size, &payload, &payload_size) == 0) {
    /* Process payload (RPC request/response) */
    uvrpc_decode_request(payload, payload_size, ...);
}
```

## 总结

新的设计遵循"少即是多"的原则：
- **更少的接口**：从 7 个函数减少到 5 个
- **更少的依赖**：传输层不依赖 RPC 层
- **更少的代码**：移除了虚拟函数表、union 等机制
- **更清晰的结构**：三层架构，职责明确