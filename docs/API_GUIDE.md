# UVRPC API 使用指南

## 重要原则

**用户负责运行事件循环**

UVRPC的客户端和服务端都不负责运行libuv事件循环。用户必须：
1. 创建自己的事件循环
2. 将loop注入到配置中
3. 调用uv_run()运行事件循环

## 代码生成

UVRPC 支持 DSL（领域特定语言）自动生成代码，使用 FlatBuffers schema 定义服务。

### 构建代码生成器

```bash
# 构建包含 FlatCC 的代码生成器
make generator-with-flatcc

# 生成器位于 dist/uvrpcc/uvrpcc
```

### 生成代码

```bash
# 使用打包的代码生成器（推荐）
./dist/uvrpcc/uvrpcc schema/rpc_api.fbs -o generated

# 生成 Broadcast 模式代码
./dist/uvrpcc/uvrpcc schema/rpc_broadcast.fbs -o generated

# 或使用 Python（需要安装依赖）
python3 tools/rpc_dsl_generator_with_flatcc.py \
    --flatcc deps/flatcc/bin/flatcc \
    -o generated \
    schema/rpc_api.fbs
```

### Schema 定义

#### Server/Client 模式

```flatbuffers
namespace rpc;

// 定义请求/响应类型
table MathAddRequest {
    a: int32;
    b: int32;
}

table MathAddResponse {
    result: int32;
}

// 定义服务
rpc_service MathService {
    Add(MathAddRequest):MathAddResponse;
}
```

#### Broadcast 模式

```flatbuffers
namespace rpc;

// 定义消息类型
table NewsPublishRequest {
    title: string;
    content: string;
    timestamp: int64;
}

table NewsPublishResponse {
    success: bool;
    message: string;
}

// 定义广播服务（服务名包含 "Broadcast" 表示广播模式）
rpc_service BroadcastService {
    PublishNews(NewsPublishRequest):NewsPublishResponse;
}
```

## Server/Client 模式使用

### 服务器端

#### 基本流程

```c
#include "rpc_math_api.h"

// 1. 创建事件循环（用户负责）
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建服务器（使用生成的API）
uvrpc_server_t* server = uvrpc_math_create_server(&loop, "tcp://127.0.0.1:5555");
if (!server) {
    fprintf(stderr, "Failed to create server\n");
    return 1;
}

// 3. 启动服务器（不运行loop）
if (uvrpc_math_start_server(server) != UVRPC_OK) {
    fprintf(stderr, "Failed to start server\n");
    return 1;
}

// 4. 运行事件循环（用户负责）
uv_run(&loop, UV_RUN_DEFAULT);

// 5. 清理
uvrpc_math_stop_server(server);
uvrpc_math_free_server(server);
uv_loop_close(&loop);
```

#### 实现业务逻辑

生成的代码要求你实现一个统一的处理器函数：

```c
// 处理所有 RPC 请求
uvrpc_error_t uvrpc_math_handle_request(const char* method_name, 
                                        const void* request,
                                        uvrpc_request_t* req) {
    
    // 解析请求
    if (strcmp(method_name, "Add") == 0) {
        const rpc_MathAddRequest_table_t* req_data = 
            rpc_MathAddRequest_as_root(request);
        
        int32_t a = rpc_MathAddRequest_a(req_data);
        int32_t b = rpc_MathAddRequest_b(req_data);
        
        // 业务逻辑
        int32_t result = a + b;
        
        // 构建响应
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_MathAddResponse_create(&builder, result);
        rpc_MathAddResponse_ref_t resp = rpc_MathAddResponse_as_root(&builder);
        
        // 发送响应
        const uint8_t* buf = flatcc_builder_get_direct_buffer(&builder);
        size_t size = flatcc_builder_get_buffer_size(&builder);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_clear(&builder);
    }
    
    return UVRPC_OK;
}
```

### 客户端

#### 异步调用

```c
#include "rpc_math_api.h"

// 1. 创建事件循环（用户负责）
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建客户端（使用生成的API）
uvrpc_client_t* client = uvrpc_math_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);
if (!client) {
    fprintf(stderr, "Failed to create client\n");
    return 1;
}

// 3. 连接服务器
uv_rpc_connect(client);  // 需要等待连接完成

// 4. 定义回调
void on_add_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        const rpc_MathAddResponse_table_t* result = 
            rpc_MathAddResponse_as_root(resp->data);
        printf("Result: %d\n", rpc_MathAddResponse_result(result));
    }
}

// 5. 调用 RPC 方法
uvrpc_math_add(client, on_add_response, NULL, 10, 20);

// 6. 运行事件循环（用户负责）
uv_run(&loop, UV_RUN_DEFAULT);

// 7. 清理
uvrpc_math_free_client(client);
uv_loop_close(&loop);
```

#### 同步调用（使用 Async/Await）

```c
#include "rpc_math_api.h"

uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_client_t* client = uvrpc_math_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);

// 同步调用（内部使用 async/await）
rpc_MathAddResponse_table_t response;
uvrpc_error_t err = uvrpc_math_add_sync(client, &response, 10, 20, 5000);

if (err == UVRPC_OK) {
    printf("Result: %d\n", rpc_MathAddResponse_result(&response));
} else {
    printf("Error: %d\n", err);
}

uvrpc_math_free_client(client);
uv_loop_close(&loop);
```

## Broadcast 模式使用

### Publisher

```c
#include "rpc_broadcast_api.h"

// 1. 创建事件循环
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建发布者（使用生成的API）
uvrpc_publisher_t* publisher = uvrpc_broadcast_create_publisher(&loop, "udp://0.0.0.0:5555");
if (!publisher) {
    fprintf(stderr, "Failed to create publisher\n");
    return 1;
}

// 3. 启动发布者
if (uvrpc_broadcast_start_publisher(publisher) != UVRPC_OK) {
    fprintf(stderr, "Failed to start publisher\n");
    return 1;
}

// 4. 定义发布回调
void on_published(int status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("Message published successfully\n");
    }
}

// 5. 发布消息
const char* title = "Breaking News";
const char* content = "This is important news";
int64_t timestamp = 1234567890;
const char* author = "Admin";

uvrpc_broadcast_publish_publish_news(publisher, on_published, NULL, 
                                     title, content, timestamp, author);

// 6. 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);

// 7. 清理
uvrpc_broadcast_stop_publisher(publisher);
uvrpc_broadcast_free_publisher(publisher);
uv_loop_close(&loop);
```

### Subscriber

```c
#include "rpc_broadcast_api.h"

// 1. 创建事件循环
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建订阅者（使用生成的API）
uvrpc_subscriber_t* subscriber = uvrpc_broadcast_create_subscriber(&loop, "udp://127.0.0.1:5555");
if (!subscriber) {
    fprintf(stderr, "Failed to create subscriber\n");
    return 1;
}

// 3. 连接到发布者
if (uvrpc_broadcast_connect_subscriber(subscriber) != UVRPC_OK) {
    fprintf(stderr, "Failed to connect\n");
    return 1;
}

// 4. 定义接收回调
void on_news_received(const rpc_NewsPublishResponse_table_t* response, void* ctx) {
    printf("Received news:\n");
    printf("  Success: %s\n", rpc_NewsPublishResponse_success(response) ? "Yes" : "No");
    printf("  Message: %s\n", rpc_NewsPublishResponse_message(response));
}

// 5. 订阅消息
uvrpc_broadcast_subscribe_publish_news(subscriber, on_news_received, NULL);

// 6. 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);

// 7. 清理
uvrpc_broadcast_unsubscribe_publish_news(subscriber);
uvrpc_broadcast_disconnect_subscriber(subscriber);
uvrpc_broadcast_free_subscriber(subscriber);
uv_loop_close(&loop);
```

#### 订阅所有消息

```c
// 接收所有广播消息的回调
void on_any_message(const char* method_name, const uint8_t* data, size_t size, void* ctx) {
    printf("Received message from method: %s\n", method_name);
    // 根据 method_name 解析不同的消息类型
}

// 订阅所有方法
uvrpc_broadcast_subscribe_all(subscriber, on_any_message, NULL);

// 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);

// 取消订阅所有
uvrpc_broadcast_unsubscribe_all(subscriber);
```

## 高级特性

### 自定义传输层

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

// TCP (默认)
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

// UDP
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);

// IPC
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);

// INPROC (进程内，零拷贝)
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
```

### 配置通信模式

```c
// Server/Client 模式（默认）
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

// Broadcast 模式
uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
```

### 循环注入

```c
// 支持多个服务共享同一个事件循环
uv_loop_t loop;
uv_loop_init(&loop);

// 创建多个服务器
uvrpc_server_t* server1 = uvrpc_math_create_server(&loop, "tcp://127.0.0.1:5555");
uvrpc_server_t* server2 = uvrpc_user_create_server(&loop, "tcp://127.0.0.1:5556");

// 启动所有服务器
uvrpc_math_start_server(server1);
uvrpc_user_start_server(server2);

// 运行一个事件循环处理所有服务器
uv_run(&loop, UV_RUN_DEFAULT);
```

## 完整示例

参见 `examples/` 目录下的示例代码：

- `simple_server.c` - 简单服务器
- `simple_client.c` - 简单客户端
- `broadcast_publisher.c` - 广播发布者
- `broadcast_subscriber.c` - 广播订阅者
- `async_await_demo.c` - Async/Await 示例
- `multi_service_loop_reuse.c` - 多服务共享循环