# UVRPC 快速开始指南

本指南将帮助你在 5 分钟内开始使用 UVRPC。

## 安装

### 前置要求

- GCC >= 4.8
- CMake >= 3.5
- make
- Python 3.6+ (用于代码生成)

### 编译

```bash
# 克隆项目（包含所有子模块）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 使用 Makefile 编译（推荐）
make deps          # 同步依赖
make build         # 编译项目

# 或使用脚本
./build.sh

# 或使用 CMake
mkdir build && cd build
cmake ..
make
```

编译完成后，可执行文件位于 `dist/bin/` 目录。

## 5 分钟快速体验

### 步骤 1：启动服务器

在终端 1 中运行：

```bash
./dist/bin/simple_server
```

输出：
```
[SERVER] Running on tcp://127.0.0.1:5555
```

### 步骤 2：运行客户端

在终端 2 中运行：

```bash
./dist/bin/simple_client
```

输出：
```
[CLIENT] Connected to tcp://127.0.0.1:5555
Received: Hello, UVRPC!
```

恭喜！你已经成功运行了第一个 UVRPC 程序。

## 使用代码生成器

UVRPC 提供 DSL（领域特定语言）来自动生成代码，大大简化开发流程。

### 定义服务

创建 `my_service.fbs` 文件：

```flatbuffers
namespace myapp;

// 定义请求类型
table AddRequest {
    a: int32;
    b: int32;
}

// 定义响应类型
table AddResponse {
    result: int32;
}

// 定义服务
rpc_service CalcService {
    Add(AddRequest):AddResponse;
}
```

### 生成代码

```bash
# 使用 Makefile
make generate SERVICE=my_service.fbs

# 或直接使用生成器
python tools/rpc_dsl_generator.py \
    --flatcc build/flatcc/flatcc \
    -o generated \
    my_service.fbs
```

生成的文件：
- `myapp_calc_api.h` - API 头文件
- `myapp_calc_client.c` - 客户端实现
- `myapp_calc_server_stub.c` - 服务器存根

### 使用生成的代码

#### 服务器端

```c
#include "myapp_calc_api.h"

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建服务器
    uvrpc_server_t* server = uvrpc_calc_create_server(&loop, "tcp://127.0.0.1:5555");
    uvrpc_calc_start_server(server);
    
    // 运行事件循环
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_calc_stop_server(server);
    uvrpc_calc_free_server(server);
    uv_loop_close(&loop);
    return 0;
}

// 实现业务逻辑（必须实现）
uvrpc_error_t uvrpc_calc_handle_request(const char* method_name, 
                                        const void* request,
                                        uvrpc_request_t* req) {
    if (strcmp(method_name, "Add") == 0) {
        const myapp_AddRequest_table_t* req_data = myapp_AddRequest_as_root(request);
        int32_t a = myapp_AddRequest_a(req_data);
        int32_t b = myapp_AddRequest_b(req_data);
        
        // 业务逻辑
        int32_t result = a + b;
        
        // 构建响应
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        myapp_AddResponse_create(&builder, result);
        
        const uint8_t* buf = flatcc_builder_get_direct_buffer(&builder);
        size_t size = flatcc_builder_get_buffer_size(&builder);
        
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        flatcc_builder_clear(&builder);
    }
    return UVRPC_OK;
}
```

#### 客户端

```c
#include "myapp_calc_api.h"

void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        const myapp_AddResponse_table_t* result = myapp_AddResponse_as_root(resp->data);
        printf("Result: %d\n", myapp_AddResponse_result(result));
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建客户端
    uvrpc_client_t* client = uvrpc_calc_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);
    
    // 调用 RPC 方法
    uvrpc_calc_add(client, on_response, NULL, 10, 20);
    
    // 运行事件循环
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_calc_free_client(client);
    uv_loop_close(&loop);
    return 0;
}
```

## 广播模式

### 定义广播服务

```flatbuffers
namespace myapp;

table NewsRequest {
    title: string;
    content: string;
}

table NewsResponse {
    success: bool;
}

// 广播模式：服务名包含 "Broadcast" 表示广播模式
rpc_service BroadcastService {
    Publish(NewsRequest):NewsResponse;
}
```

### 生成广播代码

```bash
python tools/rpc_dsl_generator.py \
    --flatcc build/flatcc/flatcc \
    -o generated \
    my_broadcast_service.fbs
```

### 使用广播 API

#### 发布者

```c
#include "myapp_news_broadcast_api.h"

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建发布者
    uvrpc_publisher_t* publisher = uvrpc_news_create_publisher(&loop, "udp://0.0.0.0:5555");
    uvrpc_news_start_publisher(publisher);
    
    // 发布消息
    void on_published(int status, void* ctx) {
        printf("Published\n");
    }
    
    uvrpc_news_publish_publish(publisher, on_published, NULL, "Title", "Content");
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_news_stop_publisher(publisher);
    uvrpc_news_free_publisher(publisher);
    uv_loop_close(&loop);
    return 0;
}
```

#### 订阅者

```c
#include "myapp_news_broadcast_api.h"

void on_news(const myapp_NewsResponse_table_t* response, void* ctx) {
    printf("Received news\n");
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建订阅者
    uvrpc_subscriber_t* subscriber = uvrpc_news_create_subscriber(&loop, "udp://127.0.0.1:5555");
    uvrpc_news_connect_subscriber(subscriber);
    
    // 订阅消息
    uvrpc_news_subscribe_publish(subscriber, on_news, NULL);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_news_unsubscribe_publish(subscriber);
    uvrpc_news_disconnect_subscriber(subscriber);
    uvrpc_news_free_subscriber(subscriber);
    uv_loop_close(&loop);
    return 0;
}
```

## 核心概念

### 1. 通信模式

UVRPC 支持两种通信模式：

#### 客户端-服务器（CS）模式
```c
// 服务器端
uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "method_name", handler, NULL);
uvrpc_server_start(server);

// 客户端
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
uvrpc_client_call(client, "method_name", data, size, callback, ctx);
```

#### 广播模式
```c
// 发布者
uvrpc_publisher_t* publisher = uvrpc_publisher_create(config);
uvrpc_publisher_start(publisher);
uvrpc_publisher_publish(publisher, "topic", data, size, callback, ctx);

// 订阅者
uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
uvrpc_subscriber_connect(subscriber);
uvrpc_subscriber_subscribe(subscriber, "topic", callback, ctx);
```

### 2. 传输层

UVRPC 支持多种传输层，通过配置切换：

```c
// TCP (默认)
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

// UDP
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);

// IPC (进程间通信)
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);

// INPROC (进程内，零拷贝)
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
```

### 3. 循环注入

UVRPC **不负责运行事件循环**，用户必须：

```c
// 1. 创建自己的事件循环
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 注入到配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);

// 3. 创建服务器/客户端
uvrpc_server_t* server = uvrpc_server_create(config);

// 4. 用户负责运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);

// 5. 清理
uv_loop_close(&loop);
```

### 4. 异步 API

所有 API 都是异步的，使用回调处理结果：

```c
void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status == UVRPC_OK) {
        // 处理响应
    }
}

uvrpc_client_call(client, "method", data, size, on_response, ctx);
```

### 5. 同步 API（Async/Await）

生成的代码提供同步 API，内部使用 async/await：

```c
// 同步调用
rpc_Response_table_t response;
uvrpc_error_t err = uvrpc_method_sync(client, &response, params, timeout_ms);

if (err == UVRPC_OK) {
    // 使用响应
}
```

## 下一步

- 阅读 [API 使用指南](API_GUIDE.md) 了解详细 API
- 查看 [examples/](../examples/) 目录下的示例代码
- 运行性能测试：`make benchmark`
- 阅读 [设计哲学](DESIGN_PHILOSOPHY.md) 了解架构设计

## 常见问题

### Q: 如何选择传输层？

- **TCP**: 可靠传输，适合 CS 模式
- **UDP**: 高性能，适合广播模式
- **IPC**: 同机进程通信，性能优于 TCP
- **INPROC**: 同进程内通信，零拷贝，性能最高

### Q: 事件循环可以共享吗？

可以！多个服务可以共享同一个事件循环：

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_server_t* server1 = uvrpc_service1_create_server(&loop, "tcp://127.0.0.1:5555");
uvrpc_server_t* server2 = uvrpc_service2_create_server(&loop, "tcp://127.0.0.1:5556");

uv_run(&loop, UV_RUN_DEFAULT);  // 一个循环处理所有服务
```

### Q: 如何处理错误？

所有 API 返回 `uvrpc_error_t`：

```c
uvrpc_error_t err = uvrpc_client_connect(client);
if (err != UVRPC_OK) {
    fprintf(stderr, "Connection failed: %d\n", err);
}
```

### Q: 如何调试？

启用调试日志：

```c
#define UVRPC_DEBUG
#include "uvrpc.h"
```

或使用 Valgrind 检测内存泄漏：

```bash
valgrind --leak-check=full ./dist/bin/simple_server
```