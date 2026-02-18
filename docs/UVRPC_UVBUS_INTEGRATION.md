# UVRPC 完全封装 UVBus 设计

## 设计原则

### 核心哲学
- **极简**：用户只需使用 UVRPC API，无需了解 UVBus
- **封装**：UVBus 是实现细节，对用户完全隐藏
- **透明**：UVRPC 内部使用 UVBus，但不暴露给用户
- **一致性**：UVRPC 的 API 保持不变，内部实现优化

## 架构层次

```
┌─────────────────────────────────────┐
│         用户代码                    │
│  uvrpc_server_create()              │
│  uvrpc_client_create()              │
│  uvrpc_server_register()            │
│  uvrpc_client_call()                │
└──────────────┬──────────────────────┘
               │
               ↓
┌─────────────────────────────────────┐
│      UVRPC (用户可见 API)           │
│  - 服务器/客户端管理                 │
│  - RPC 协议处理                     │
│  - 请求/响应路由                    │
│  - 回调管理                         │
└──────────────┬──────────────────────┘
               │ 内部实现
               ↓
┌─────────────────────────────────────┐
│      UVBus (实现细节)               │
│  - TCP/UDP/IPC/INPROC 传输          │
│  - 连接管理                         │
│  - 字节发送/接收                    │
└──────────────┬──────────────────────┘
               │
               ↓
┌─────────────────────────────────────┐
│         libuv (事件循环)            │
└─────────────────────────────────────┘
```

## UVRPC 内部使用 UVBus

### 服务器实现

```c
/* UVRPC 服务器结构（用户可见） */
struct uvrpc_server {
    uv_loop_t* loop;
    char* address;
    
    /* UVBus 实例（内部使用，不暴露给用户） */
    uvbus_t* uvbus;
    
    /* RPC 状态 */
    uvrpc_handler_t* handlers;
    int is_running;
};

/* 创建服务器 */
uvrpc_server_t* uvrpc_server_create(uvrpc_config_t* config) {
    uvrpc_server_t* server = uvrpc_calloc(1, sizeof(uvrpc_server_t));
    server->loop = config->loop;
    server->address = uvrpc_strdup(config->address);
    
    /* 解析传输类型 */
    uvbus_transport_type_t transport = parse_transport_type(config->transport);
    
    /* 内部创建 UVBus 实例（用户不知道） */
    server->uvbus = uvbus_server_new(config->loop, transport, config->address);
    
    /* 设置内部回调 */
    uvbus_server_set_recv_callback(server->uvbus, server_on_recv, server);
    uvbus_server_set_error_callback(server->uvbus, server_on_error, server);
    
    return server;
}

/* 启动服务器 */
uvrpc_error_t uvrpc_server_start(uvrpc_server_t* server) {
    /* 内部调用 UVBus */
    uvbus_error_t err = uvbus_server_listen(server->uvbus);
    if (err != UVBUS_OK) {
        return UVRPC_ERROR_TRANSPORT;
    }
    
    server->is_running = 1;
    return UVRPC_OK;
}

/* 内部接收回调（用户不可见） */
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

/* 发送响应（内部使用 UVBus） */
static uvrpc_error_t server_send_response(uvrpc_server_t* server,
                                          void* client_id,
                                          const uint8_t* data,
                                          size_t size) {
    /* 内部调用 UVBus */
    uvbus_error_t err = uvbus_server_send_to(server->uvbus, data, size, client_id);
    return err == UVBUS_OK ? UVRPC_OK : UVRPC_ERROR_TRANSPORT;
}

/* 释放服务器 */
void uvrpc_server_free(uvrpc_server_t* server) {
    /* 内部释放 UVBus */
    if (server->uvbus) {
        uvbus_server_free(server->uvbus);
        server->uvbus = NULL;
    }
    
    /* 清理其他资源 */
    if (server->address) {
        uvrpc_free(server->address);
    }
    
    uvrpc_free(server);
}
```

### 客户端实现

```c
/* UVRPC 客户端结构（用户可见） */
struct uvrpc_client {
    uv_loop_t* loop;
    char* address;
    
    /* UVBus 实例（内部使用，不暴露给用户） */
    uvbus_t* uvbus;
    
    /* RPC 状态 */
    uint32_t next_msgid;
    uvrpc_call_t* pending_calls;
    int is_connected;
};

/* 创建客户端 */
uvrpc_client_t* uvrpc_client_create(uvrpc_config_t* config) {
    uvrpc_client_t* client = uvrpc_calloc(1, sizeof(uvrpc_client_t));
    client->loop = config->loop;
    client->address = uvrpc_strdup(config->address);
    client->next_msgid = 1;
    
    /* 解析传输类型 */
    uvbus_transport_type_t transport = parse_transport_type(config->transport);
    
    /* 内部创建 UVBus 实例（用户不知道） */
    client->uvbus = uvbus_client_new(config->loop, transport, config->address);
    
    /* 设置内部回调 */
    uvbus_client_set_recv_callback(client->uvbus, client_on_recv, client);
    uvbus_client_set_connect_callback(client->uvbus, client_on_connect, client);
    uvbus_client_set_error_callback(client->uvbus, client_on_error, client);
    
    return client;
}

/* 连接到服务器 */
uvrpc_error_t uvrpc_client_connect(uvrpc_client_t* client) {
    /* 内部调用 UVBus */
    uvbus_error_t err = uvbus_client_connect(client->uvbus);
    return err == UVBUS_OK ? UVRPC_OK : UVRPC_ERROR_TRANSPORT;
}

/* 内部接收回调（用户不可见） */
static void client_on_recv(const uint8_t* data, size_t size, void* ctx) {
    uvrpc_client_t* client = (uvrpc_client_t*)ctx;
    
    /* UVRPC 处理 RPC 响应 */
    uint32_t msgid;
    uvrpc_error_t error;
    const uint8_t* result;
    size_t result_size;
    
    if (uvrpc_decode_response(data, size, &msgid, &error,
                              &result, &result_size) == UVRPC_OK) {
        /* 触发用户回调 */
        uvrpc_call_t* call = find_call(client, msgid);
        if (call && call->callback) {
            call->callback(error, result, result_size, call->ctx);
        }
        remove_call(client, msgid);
    }
}

/* 发送 RPC 调用（内部使用 UVBus） */
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
    
    /* 记录调用 */
    add_call(client, msgid, cb, ctx);
    
    /* 内部调用 UVBus */
    uvbus_error_t err = uvbus_client_send(client->uvbus, data, size);
    uvrpc_free(data);
    
    return err == UVBUS_OK ? UVRPC_OK : UVRPC_ERROR_TRANSPORT;
}

/* 释放客户端 */
void uvrpc_client_free(uvrpc_client_t* client) {
    /* 内部释放 UVBus */
    if (client->uvbus) {
        uvbus_client_free(client->uvbus);
        client->uvbus = NULL;
    }
    
    /* 清理其他资源 */
    if (client->address) {
        uvrpc_free(client->address);
    }
    
    uvrpc_free(client);
}
```

## 用户代码示例

### 服务器端（用户不知道 UVBus）

```c
#include "uvrpc.h"

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建 UVRPC 服务器（用户不知道内部使用 UVBus） */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    
    /* 注册处理器 */
    uvrpc_server_register(server, "Calculator.Add", add_handler, NULL);
    
    /* 启动服务器 */
    uvrpc_server_start(server);
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

### 客户端（用户不知道 UVBus）

```c
#include "uvrpc.h"

void on_response(uvrpc_error_t error, const uint8_t* result, size_t size, void* ctx) {
    if (error == UVRPC_OK) {
        int32_t value = *(int32_t*)result;
        printf("Result: %d\n", value);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建 UVRPC 客户端（用户不知道内部使用 UVBus） */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    
    /* 连接 */
    uvrpc_client_connect(client);
    
    /* 调用 RPC */
    int32_t a = 10, b = 20;
    uvrpc_client_call(client, "Calculator.Add", 
                       (uint8_t*)&a, sizeof(a), on_response, NULL);
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

## 封装的优势

### 1. 简化用户代码
- 用户只需了解 UVRPC API
- 无需知道 UVBus 的存在
- 无需关心传输层细节

### 2. 保持向后兼容
- UVRPC API 保持不变
- 现有代码无需修改
- 内部优化对用户透明

### 3. 清晰的职责
- **用户**：使用 UVRPC API
- **UVRPC**：处理 RPC 语义，内部使用 UVBus
- **UVBus**：处理传输层，对用户隐藏

### 4. 易于测试
- UVRPC 可以 mock UVBus 进行单元测试
- UVBus 可以独立测试传输层

### 5. 灵活的实现
- 可以替换不同的 UVBus 实现
- 可以添加新的传输协议而不影响 UVRPC API

## 内部实现细节

### 错误码转换

```c
/* UVRPC 内部转换 UVBus 错误码 */
static uvrpc_error_t convert_uvbus_error(uvbus_error_t uvbus_err) {
    switch (uvbus_err) {
        case UVBUS_OK:
            return UVRPC_OK;
        case UVBUS_ERROR_INVALID_PARAM:
            return UVRPC_ERROR_INVALID_PARAM;
        case UVBUS_ERROR_NO_MEMORY:
            return UVRPC_ERROR_NO_MEMORY;
        case UVBUS_ERROR_NOT_CONNECTED:
            return UVRPC_ERROR_NOT_CONNECTED;
        case UVBUS_ERROR_TIMEOUT:
            return UVRPC_ERROR_TIMEOUT;
        case UVBUS_ERROR_IO:
            return UVRPC_ERROR_TRANSPORT;
        default:
            return UVRPC_ERROR;
    }
}
```

### 传输类型映射

```c
/* UVRPC 内部映射传输类型 */
static uvbus_transport_type_t convert_transport_type(uvrpc_transport_type_t uvrpc_type) {
    switch (uvrpc_type) {
        case UVRPC_TRANSPORT_TCP:
            return UVBUS_TRANSPORT_TCP;
        case UVRPC_TRANSPORT_UDP:
            return UVBUS_TRANSPORT_UDP;
        case UVRPC_TRANSPORT_IPC:
            return UVBUS_TRANSPORT_IPC;
        case UVRPC_TRANSPORT_INPROC:
            return UVBUS_TRANSPORT_INPROC;
        default:
            return UVBUS_TRANSPORT_TCP;  /* 默认 TCP */
    }
}
```

## 总结

UVRPC 完全封装 UVBus 的设计：

1. **用户视角**：只看到 UVRPC API，不知道 UVBus 的存在
2. **实现视角**：UVRPC 内部使用 UVBus 进行传输
3. **架构清晰**：职责分离，层次分明
4. **易于维护**：可以独立优化 UVBus 或 UVRPC
5. **向后兼容**：现有代码无需修改

这种设计完全符合 UVRPC 的极简设计哲学！