# UVBus 重构示例

## 使用新的传输层

### 1. 创建 TCP 服务器

```c
#include "uv_transport.h"
#include "uv_frame.h"
#include "uvrpc.h"

static void on_recv(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer, void* ctx) {
    /* Decode frame */
    const uint8_t* payload;
    size_t payload_size;
    if (uv_frame_decode(data, size, &payload, &payload_size) != 0) {
        printf("Invalid frame\n");
        return;
    }
    
    /* Decode RPC request */
    uint32_t msgid;
    char* method;
    const uint8_t* params;
    size_t params_size;
    
    if (uvrpc_decode_request(payload, payload_size, &msgid, &method, &params, &params_size) == UVRPC_OK) {
        printf("Received request: msgid=%u, method=%s\n", msgid, method);
        
        /* Handle request */
        uvrpc_request_t req;
        req.msgid = msgid;
        req.method = method;
        req.params = (uint8_t*)params;
        req.params_size = params_size;
        req.client_ctx = peer;
        
        handler(&req, ctx);
        
        /* Cleanup */
        if (method) uvrpc_free(method);
    }
    
    /* Note: data will be freed by transport layer */
}

static void on_connect(uv_transport_t* transport, int status, void* ctx) {
    if (status == 0) {
        printf("Client connected\n");
    } else {
        printf("Connection error: %d\n", status);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create transport config */
    uv_transport_config_t config = {
        .loop = &loop,
        .address = "tcp://127.0.0.1:7003",
        .is_server = 1,
        .on_recv = on_recv,
        .on_connect = on_connect,
        .callback_ctx = NULL
    };
    
    /* Create transport */
    uv_transport_t* transport = uv_transport_create(UV_TRANSPORT_TCP, &config);
    if (!transport) {
        printf("Failed to create transport\n");
        return 1;
    }
    
    /* Start listening */
    if (uv_transport_listen(transport) != 0) {
        printf("Failed to listen\n");
        uv_transport_destroy(transport);
        return 1;
    }
    
    /* Run event loop */
    printf("Server running on %s\n", config.address);
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    uv_transport_stop(transport);
    uv_transport_destroy(transport);
    uv_loop_close(&loop);
    
    return 0;
}
```

### 2. 创建 TCP 客户端

```c
#include "uv_transport.h"
#include "uv_frame.h"
#include "uvrpc.h"

static void on_recv(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer, void* ctx) {
    /* Decode frame */
    const uint8_t* payload;
    size_t payload_size;
    if (uv_frame_decode(data, size, &payload, &payload_size) != 0) {
        return;
    }
    
    /* Decode RPC response */
    uint32_t msgid;
    uint8_t* result;
    size_t result_size;
    int32_t error_code;
    const char* error_msg;
    
    if (uvrpc_decode_response(payload, payload_size, &msgid, &result, &result_size, 
                             &error_code, &error_msg) == UVRPC_OK) {
        printf("Received response: msgid=%u\n", msgid);
        
        /* Call user callback */
        callback(msgid, result, result_size, error_code, error_msg, ctx);
        
        /* Cleanup */
        if (result) uvrpc_free(result);
        if (error_msg) uvrpc_free(error_msg);
    }
}

static void on_connect(uv_transport_t* transport, int status, void* ctx) {
    if (status == 0) {
        printf("Connected to server\n");
        /* Send RPC request */
        send_rpc_request(transport);
    } else {
        printf("Connection failed: %d\n", status);
    }
}

void send_rpc_request(uv_transport_t* transport) {
    /* Encode RPC request */
    uint32_t msgid = 1;
    const char* method = "add";
    int32_t params[2] = {10, 20};
    
    uint8_t* req_data;
    size_t req_size;
    
    if (uvrpc_encode_request(msgid, method, (uint8_t*)params, sizeof(params),
                             &req_data, &req_size) != UVRPC_OK) {
        printf("Failed to encode request\n");
        return;
    }
    
    /* Encode as frame */
    uv_frame_t* frame = uv_frame_encode(req_data, req_size);
    if (!frame) {
        printf("Failed to encode frame\n");
        uvrpc_free(req_data);
        return;
    }
    
    /* Send frame */
    if (uv_transport_send(transport, frame->data, frame->size) != 0) {
        printf("Failed to send\n");
    }
    
    /* Cleanup */
    uv_frame_free(frame);
    uvrpc_free(req_data);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* Create transport config */
    uv_transport_config_t config = {
        .loop = &loop,
        .address = "tcp://127.0.0.1:7003",
        .is_server = 0,
        .on_recv = on_recv,
        .on_connect = on_connect,
        .callback_ctx = NULL
    };
    
    /* Create transport */
    uv_transport_t* transport = uv_transport_create(UV_TRANSPORT_TCP, &config);
    if (!transport) {
        printf("Failed to create transport\n");
        return 1;
    }
    
    /* Connect to server */
    if (uv_transport_connect(transport) != 0) {
        printf("Failed to connect\n");
        uv_transport_destroy(transport);
        return 1;
    }
    
    /* Run event loop */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* Cleanup */
    uv_transport_disconnect(transport);
    uv_transport_destroy(transport);
    uv_loop_close(&loop);
    
    return 0;
}
```

## 对比：旧接口 vs 新接口

### 旧接口（复杂）

```c
/* 创建配置 */
uvbus_config_t* config = uvbus_config_new();
uvbus_config_set_loop(config, &loop);
uvbus_config_set_transport(config, UVBUS_TRANSPORT_TCP);
uvbus_config_set_address(config, "tcp://127.0.0.1:7003");
uvbus_config_set_recv_callback(config, on_recv, NULL);

/* 创建 bus */
uvbus_t* bus = uvbus_new(config);

/* 监听/连接 */
if (is_server) {
    uvbus_listen(bus);
} else {
    uvbus_connect(bus);
}

/* 发送 - 需要手动添加帧格式 */
size_t frame_size = 4 + payload_size;
uint8_t* frame_data = malloc(frame_size);
frame_data[0] = (payload_size >> 24) & 0xFF;
frame_data[1] = (payload_size >> 16) & 0xFF;
frame_data[2] = (payload_size >> 8) & 0xFF;
frame_data[3] = payload_size & 0xFF;
memcpy(frame_data + 4, payload, payload_size);
uvbus_send(bus, frame_data, frame_size);
free(frame_data);

/* 接收 - 需要手动解析帧格式 */
void on_recv(const uint8_t* data, size_t size, void* client_ctx, void* server_ctx) {
    if (size < 4) return;
    size_t payload_size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (size != 4 + payload_size) return;
    const uint8_t* payload = data + 4;
    /* Process payload */
}

/* 清理 */
uvbus_free(bus);
uvbus_config_free(config);
```

### 新接口（简洁）

```c
/* 创建配置 */
uv_transport_config_t config = {
    .loop = &loop,
    .address = "tcp://127.0.0.1:7003",
    .is_server = is_server,
    .on_recv = on_recv,
    .callback_ctx = NULL
};

/* 创建 transport */
uv_transport_t* transport = uv_transport_create(UV_TRANSPORT_TCP, &config);

/* 监听/连接 */
if (is_server) {
    uv_transport_listen(transport);
} else {
    uv_transport_connect(transport);
}

/* 发送 - 使用帧层 */
uv_frame_t* frame = uv_frame_encode(payload, payload_size);
uv_transport_send(transport, frame->data, frame->size);
uv_frame_free(frame);

/* 接收 - 使用帧层 */
void on_recv(uv_transport_t* transport, const uint8_t* data, size_t size, void* peer, void* ctx) {
    const uint8_t* payload;
    size_t payload_size;
    if (uv_frame_decode(data, size, &payload, &payload_size) == 0) {
        /* Process payload */
    }
    /* Note: data will be freed by transport layer */
}

/* 清理 */
if (is_server) {
    uv_transport_stop(transport);
} else {
    uv_transport_disconnect(transport);
}
uv_transport_destroy(transport);
```

## 优势对比

| 特性 | 旧接口 | 新接口 |
|------|--------|--------|
| 配置步骤 | 5+ 步 | 1 步 |
| 发送数据 | 需要手动添加帧格式 | 使用 uv_frame_encode |
| 接收数据 | 需要手动解析帧格式 | 使用 uv_frame_decode |
| 内存管理 | 手动管理帧缓冲 | uv_frame_free 自动管理 |
| 代码行数 | ~50 行 | ~30 行 |
| 可读性 | 中等 | 高 |
| 易于扩展 | 需要实现 7 个函数 | 只需实现 5 个函数 |
| 职责分离 | 混合 | 清晰（传输层 + 帧层） |

## 迁移建议

1. **先实现新接口**，不影响现有代码
2. **编写单元测试**，确保新接口正确
3. **逐步迁移** UVRPC 使用新接口
4. **清理旧代码**，删除 uvbus.c

这样的设计符合"少即是多"的原则：
- 更少的配置步骤
- 更少的代码行数
- 更少的内存管理负担
- 更清晰的职责分离
- 更易于理解和维护