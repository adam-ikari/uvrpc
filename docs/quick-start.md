# UVRPC 快速开始指南

本指南将帮助你在 5 分钟内开始使用 UVRPC。

## 安装

### 前置要求

- GCC >= 4.8
- CMake >= 3.5
- make

### 编译

```bash
# 克隆项目（包含所有子模块）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 设置依赖
./scripts/setup_deps.sh

# 编译
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
uvrpc_client_call(client, "method_name", params, size, callback, ctx);
```

#### 发布-订阅（广播）模式
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

### 2. 传输协议

UVRPC 支持 4 种传输协议，使用方式完全相同：

| 协议 | 地址格式 | 适用场景 |
|-----|---------|---------|
| TCP | `tcp://host:port` | 可靠网络通信 |
| UDP | `udp://host:port` | 高吞吐网络通信 |
| IPC | `ipc:///path/to/socket` | 本地进程间通信 |
| INPROC | `inproc://name` | 进程内通信 |

### 3. 配置

使用构建器模式配置：

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
```

## 常见使用场景

### 场景 1：简单的 RPC 调用

```c
// 服务器端
void add_handler(uvrpc_request_t* req, void* ctx) {
    int32_t a = *(int32_t*)req->params;
    int32_t b = *(int32_t*)(req->params + 4);
    int32_t result = a + b;
    
    uvrpc_request_send_response(req, UVRPC_OK, 
                                 (uint8_t*)&result, sizeof(result));
    uvrpc_request_free(req);
}

// 客户端
void response_callback(uvrpc_response_t* resp, void* ctx) {
    int32_t result = *(int32_t*)resp->result;
    printf("Result: %d\n", result);
    uvrpc_response_free(resp);
}

int32_t params[2] = {10, 20};
uvrpc_client_call(client, "Add", (uint8_t*)params, sizeof(params), 
                  response_callback, NULL);
```

### 场景 2：发布-订阅

```c
// 发布者
void publish_callback(int status, void* ctx) {
    if (status == UVRPC_OK) {
        printf("Published successfully\n");
    }
}

const char* message = "Hello, World!";
uvrpc_publisher_publish(publisher, "news", 
                        (const uint8_t*)message, strlen(message),
                        publish_callback, NULL);

// 订阅者
void subscribe_callback(const char* topic, const uint8_t* data, 
                        size_t size, void* ctx) {
    printf("Received on %s: %.*s\n", topic, (int)size, data);
}

uvrpc_subscriber_subscribe(subscriber, "news", subscribe_callback, NULL);
```

### 场景 3：使用不同传输协议

**TCP**（可靠网络通信）：
```c
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
```

**UDP**（高吞吐网络通信）：
```c
uvrpc_config_set_address(config, "udp://127.0.0.1:6000");
```

**IPC**（本地进程间通信）：
```c
uvrpc_config_set_address(config, "ipc:///tmp/uvrpc.sock");
```

**INPROC**（进程内通信）：
```c
uvrpc_config_set_address(config, "inproc://my_service");
```

### 场景 4：循环注入（多实例）

```c
// 独立事件循环
uv_loop_t loop1;
uv_loop_init(&loop1);
uvrpc_config_set_loop(config1, &loop1);

// 共享事件循环
uv_loop_t shared_loop;
uv_loop_init(&shared_loop);
uvrpc_config_set_loop(config2, &shared_loop);
```

## 性能优化

### 选择合适的传输协议

- **INPROC**：进程内通信，性能最佳（125,000+ ops/s）
- **IPC**：本地进程间通信（91,895 ops/s）
- **UDP**：高吞吐网络通信（91,685 ops/s）
- **TCP**：可靠网络通信（86,930 ops/s）

### 性能模式

```c
// 高吞吐模式（默认）
uvrpc_config_set_performance_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);

// 低延迟模式
uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);
```

### 批量处理

```c
// 批量发送请求
for (int i = 0; i < 100; i++) {
    uvrpc_client_call(client, "method", params, size, callback, ctx);
}
```

## 错误处理

```c
int ret = uvrpc_server_start(server);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Failed to start server: %d\n", ret);
    // 处理错误
}

// 在回调中检查状态
void response_callback(uvrpc_response_t* resp, void* ctx) {
    if (resp->status != UVRPC_OK) {
        fprintf(stderr, "Request failed: %d\n", resp->status);
        return;
    }
    // 处理成功响应
}
```

## 资源清理

```c
// 清理顺序很重要
uvrpc_server_free(server);      // 先释放服务器
uvrpc_config_free(config);      // 再释放配置
uv_loop_close(&loop);          // 最后关闭循环
```

## 下一步

1. **查看更多示例**：
   ```bash
   cd examples
   ls -la
   ```

2. **阅读文档**：
   - [API 参考](docs/API_REFERENCE.md)
   - [设计哲学](docs/DESIGN_PHILOSOPHY.md)
   - [构建和安装](docs/BUILD_AND_INSTALL.md)

3. **运行完整示例**：
   ```bash
   ./dist/bin/complete_example server tcp://127.0.0.1:5555
   ./dist/bin/complete_example client tcp://127.0.0.1:5555
   ```

4. **性能测试**：
   ```bash
   ./benchmark/comprehensive_perf_test.sh
   ```

## 常见问题

### Q: 如何选择传输协议？

**A**:
- 进程内通信：使用 INPROC
- 本地进程间通信：使用 IPC
- 高吞吐网络：使用 UDP
- 可靠网络：使用 TCP

### Q: 如何处理异步回调？

**A**: 所有调用都是异步的，使用回调处理响应：
```c
void callback(uvrpc_response_t* resp, void* ctx) {
    // 处理响应
}
uvrpc_client_call(client, "method", params, size, callback, ctx);
```

### Q: 如何实现重试机制？

**A**: 在回调中检查状态，失败时重新发送：
```c
void callback(uvrpc_response_t* resp, void* ctx) {
    if (resp->status != UVRPC_OK) {
        // 重试逻辑
        uvrpc_client_call(client, "method", params, size, callback, ctx);
    }
}
```

### Q: 如何调试？

**A**: 使用调试示例：
```bash
./dist/bin/debug_test
```

## 获取帮助

- 查看 [examples/README.md](examples/README.md) 了解所有示例
- 查看 [docs/](docs/) 目录了解详细文档
- 提交 Issue 获取支持

---

**祝你使用愉快！** 🚀