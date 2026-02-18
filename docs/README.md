# UVRPC - Ultra-Fast RPC Framework

一个极简、高性能的 RPC 框架，基于 libuv 事件循环和 FlatBuffers 序列化。

## 设计哲学

### 核心原则

1. **极简设计** - 最小化 API、依赖和配置
2. **零线程，零锁，零全局变量** - 所有 I/O 由 libuv 事件循环管理
3. **性能驱动** - 零拷贝、事件驱动、高性能内存分配
4. **透明度优先** - 结构体公开，用户可完全控制
5. **循环注入** - 支持自定义 libuv loop，多实例独立运行或共享循环
6. **异步统一** - 所有调用都是异步的，无同步阻塞模式
7. **多协议统一抽象** - 支持 TCP、UDP、IPC、INPROC，使用方式完全相同

### 架构层次

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: 应用层（用户创建）                          │
│  - 服务处理器 (uvrpc_handler_t)                     │
│  - 客户端回调 (uvrpc_callback_t)                    │
│  - 完全独立，不受生成影响                             │
├─────────────────────────────────────────────────────┤
│  Layer 2: RPC API 层                                 │
│  - 服务端 API (uvrpc_server_t)                      │
│  - 客户端 API (uvrpc_client_t)                      │
│  - 发布/订阅 API (uvrpc_publisher/subscriber_t)     │
│  - 统一配置 (uvrpc_config_t)                        │
├─────────────────────────────────────────────────────┤
│  Layer 1: 传输层                                     │
│  - TCP (uv_tcp_t)                                   │
│  - UDP (uv_udp_t)                                   │
│  - IPC (uv_pipe_t)                                  │
│  - INPROC (内存通信)                                 │
├─────────────────────────────────────────────────────┤
│  Layer 0: 核心库层                                    │
│  - 事件循环 (libuv)                                 │
│  - 序列化 (FlatCC/FlatBuffers)                      │
│  - 内存分配 (mimalloc/system/custom)                │
│  - 性能优化 (零拷贝、批量处理)                       │
└─────────────────────────────────────────────────────┘
```

## 特性

- **极简设计**：清晰的 API，构建器模式配置
- **高性能**：基于 libuv 和 FlatBuffers 的高效序列化
- **事件驱动**：基于 libuv，完全异步非阻塞
- **多传输支持**：TCP、UDP、IPC、INPROC
- **零拷贝**：FlatBuffers 二进制序列化，最小化内存拷贝
- **循环注入**：支持自定义 libuv loop，多实例独立运行或共享循环
- **内存分配器**：支持 mimalloc、系统分配器、自定义分配器
- **类型安全**：FlatBuffers DSL 生成类型安全的 API，编译时检查
- **代码生成**：使用 FlatBuffers DSL 声明服务，自动生成客户端/服务端代码
- **多实例支持**：同一进程可创建多个独立实例，支持独立或共享事件循环
- **单线程模型**：无锁设计，避免临界区通信

## 性能指标

### CS 模式（客户端-服务器）

| 传输层 | 吞吐量 (ops/s) | 平均延迟 | 内存占用 | 成功率 | 适用场景 |
|--------|----------------|----------|---------|--------|---------|
| **INPROC** | 125,000+ | 0.03 ms | 1 MB | 100% | 进程内高性能通信 |
| **IPC** | 91,895 | 0.10 ms | 2 MB | 100% | 本地进程间通信 |
| **UDP** | 91,685 | 0.15 ms | 2 MB | 100% | 高吞吐网络（可接受丢包） |
| **TCP** | 86,930 | 0.18 ms | 2 MB | 100% | 可靠网络传输 |

### 广播模式（发布-订阅）

| 传输层 | 吞吐量 (msgs/s) | 带宽 (MB/s) | 适用场景 |
|--------|----------------|-------------|---------|
| **IPC** | 42,500 | 4.25 | 本地广播 |
| **UDP** | 40,000 | 4.00 | 网络广播 |
| **TCP** | 支持 | 支持 | 可靠广播 |

### 性能特点
- **INPROC**: 零拷贝同步执行，适合同进程内高性能通信，延迟极低
- **IPC**: 无网络开销，适合本地进程间通信，性能优于网络传输
- **UDP**: 无连接协议，适合高吞吐、可容忍丢失的场景，延迟较低
- **TCP**: 可靠传输，适合需要保证数据完整性的场景

### 测试配置
- 构建模式: Release (-O2 优化，无调试符号)
- 测试时长: 2 秒
- 批处理大小: 50-100 请求
- 客户端数量: 1-10
- 成功率: 所有传输层 100%

**注意**: 实际性能取决于硬件配置、网络条件和负载场景。详细性能分析请查看 [benchmark/results/PERFORMANCE_ANALYSIS.md](benchmark/results/PERFORMANCE_ANALYSIS.md)。

## 依赖

- libuv (>= 1.0) - 事件循环
- FlatCC - FlatBuffers 编译器
- uthash - 哈希表
- mimalloc (可选) - 高性能内存分配器
- gtest (测试) - 单元测试框架

## 编译

```bash
# 克隆项目（包含所有子模块）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 设置依赖
./scripts/setup_deps.sh

# 使用构建脚本
./build.sh

# 或使用 CMake
mkdir build && cd build
cmake ..
make
```

## 快速开始

### 服务端示例

```c
#include "uvrpc.h"
#include <stdio.h>

void echo_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    printf("Received: %.*s\n", (int)req->params_size, req->params);
    
    uvrpc_request_send_response(req, UVRPC_OK, 
                                 req->params, req->params_size);
    uvrpc_request_free(req);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);
    
    printf("Server running on tcp://127.0.0.1:5555\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

### 客户端示例

```c
#include "uvrpc.h"
#include <stdio.h>

static int g_running = 1;

void response_callback(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    printf("Received: %.*s\n", (int)resp->result_size, resp->result);
    uvrpc_response_free(resp);
    g_running = 0;
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);
    
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_client_connect(client);
    
    const char* message = "Hello, UVRPC!";
    uvrpc_client_call(client, "echo", 
                      (uint8_t*)message, strlen(message),
                      response_callback, NULL);
    
    while (g_running) {
        uv_run(&loop, UV_RUN_ONCE);
    }
    
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

### 广播模式示例

**发布者（Publisher）示例**：

```c
#include "uvrpc.h"
#include <stdio.h>

void publish_callback(int status, void* ctx) {
    (void)ctx;
    if (status != UVRPC_OK) {
        printf("Publish failed: %d\n", status);
    }
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "udp://127.0.0.1:6000");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    uvrpc_publisher_t* publisher = uvrpc_publisher_create(config);
    uvrpc_publisher_start(publisher);
    
    const char* message = "Hello, Broadcast!";
    uvrpc_publisher_publish(publisher, "news", 
                           (const uint8_t*)message, strlen(message),
                           publish_callback, NULL);
    
    printf("Publisher running on udp://127.0.0.1:6000\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_publisher_stop(publisher);
    uvrpc_publisher_free(publisher);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

**订阅者（Subscriber）示例**：

```c
#include "uvrpc.h"
#include <stdio.h>

void subscribe_callback(const char* topic, const uint8_t* data, 
                        size_t size, void* ctx) {
    (void)ctx;
    printf("Topic: %s, Message: %.*s\n", topic, (int)size, data);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "udp://127.0.0.1:6000");
    uvrpc_config_set_comm_type(config, UVRPC_COMM_BROADCAST);
    
    uvrpc_subscriber_t* subscriber = uvrpc_subscriber_create(config);
    uvrpc_subscriber_connect(subscriber);
    uvrpc_subscriber_subscribe(subscriber, "news", 
                               subscribe_callback, NULL);
    
    printf("Subscriber running on udp://127.0.0.1:6000\n");
    uv_run(&loop, UV_RUN_DEFAULT);
    
    uvrpc_subscriber_unsubscribe(subscriber, "news");
    uvrpc_subscriber_disconnect(subscriber);
    uvrpc_subscriber_free(subscriber);
    uvrpc_config_free(config);
    uv_loop_close(&loop);
    
    return 0;
}
```

## 传输协议

UVRPC 支持多种传输协议，使用统一的抽象接口，使用方式完全相同：

### TCP 传输
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
```
**特性**：可靠的面向连接通信，适合需要可靠传输的场景

### UDP 传输
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
uvrpc_config_set_address(config, "udp://127.0.0.1:5555");
```
**特性**：无连接数据报通信，适合低延迟、可容忍丢包的场景

### IPC 传输
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
uvrpc_config_set_address(config, "ipc:///tmp/uvrpc.sock");
```
**特性**：本地进程间通信，使用 Unix Domain Socket，性能优于 TCP

### INPROC 传输
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
uvrpc_config_set_address(config, "inproc://my_service");
```
**特性**：内存内零拷贝通信，性能最优，适合同一进程内的模块通信

### 统一的使用方式
所有传输协议使用相同的 API 调用：
- 相同的服务端 API：`uvrpc_server_create()`, `uvrpc_server_start()`
- 相同的客户端 API：`uvrpc_client_create()`, `uvrpc_client_connect()`, `uvrpc_client_call()`
- 相同的回调模式：连接回调、接收回调、响应回调
- 相同的错误处理：统一的错误码和错误处理模式
- **仅需修改 URL**：更换传输协议只需修改地址前缀

## 内存分配器

UVRPC 支持三种内存分配器：

```c
// 使用 mimalloc (默认)
uvrpc_allocator_init(UVRPC_ALLOCATOR_MIMALLOC, NULL);

// 使用系统分配器
uvrpc_allocator_init(UVRPC_ALLOCATOR_SYSTEM, NULL);

// 使用自定义分配器
uvrpc_custom_allocator_t custom = {
    .alloc = my_alloc,
    .free = my_free,
    // ...
};
uvrpc_allocator_init(UVRPC_ALLOCATOR_CUSTOM, &custom);
```

## 错误处理

```c
int ret = uvrpc_server_start(server);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Failed to start server: %d\n", ret);
    // 处理错误
}
```

## 测试

```bash
# 运行单元测试
./dist/bin/uvrpc_tests

# 运行集成测试
./dist/bin/test_tcp
```

## 性能测试

```bash
# 运行所有传输层的性能测试
./benchmark/simple_perf_test.sh

# 运行综合性能测试（所有模式和传输）
./benchmark/comprehensive_perf_test.sh

# 运行特定传输层的测试
./benchmark/run_benchmark.sh tcp://127.0.0.1:5555 single
./benchmark/run_benchmark.sh ipc:///tmp/uvrpc_test.sock single
./benchmark/run_benchmark.sh udp://127.0.0.1:5556 single
./benchmark/run_benchmark.sh inproc://test single

# 运行广播模式测试
./benchmark/test_broadcast.sh

# 查看性能报告
cat benchmark/results/comprehensive_report.md
cat benchmark/results/PERFORMANCE_ANALYSIS.md
```

### 性能测试结果

最新测试结果（Release 模式，-O2 优化）：

**CS 模式（客户端-服务器）**：
- **INPROC**: 125,000+ ops/s - 零拷贝，同进程内通信
- **IPC**: 91,895 ops/s - Unix Domain Socket，本地进程间通信
- **UDP**: 91,685 ops/s - 无连接协议，高吞吐
- **TCP**: 86,930 ops/s - 可靠传输，基准

**广播模式（发布-订阅）**：
- **IPC**: 42,500 msgs/s - 本地广播
- **UDP**: 40,000 msgs/s - 网络广播

**延迟性能**：
- **INPROC**: 0.03 ms 平均
- **IPC**: 0.10 ms 平均
- **UDP**: 0.15 ms 平均
- **TCP**: 0.18 ms 平均

所有传输层在中等负载下均达到 100% 成功率。详细分析请查看 [benchmark/results/](benchmark/results/)。

## 项目结构

```
uvrpc/
├── include/          # 公共头文件
├── src/             # 源代码
├── examples/        # 示例程序
├── tests/           # 测试
│   ├── unit/       # 单元测试
│   └── integration/# 集成测试
├── deps/            # 依赖 (git submodules)
├── generated/       # FlatBuffers 生成的代码
├── schema/          # FlatBuffers schema
├── build/           # 构建目录
├── dist/            # 输出目录
└── scripts/         # 辅助脚本
```

## 文档

- [编码标准](CODING_STANDARDS.md) - 代码规范和文档指南
- [Doxygen 示例](DOXYGEN_EXAMPLES.md) - Doxygen 注释示例
- [快速开始](QUICK_START.md) - 5 分钟快速入门指南
- [示例程序](examples/README.md) - 完整的示例程序文档
- [性能分析](benchmark/results/PERFORMANCE_ANALYSIS.md) - 深入的性能数据分析
- [设计哲学](docs/DESIGN_PHILOSOPHY.md) - 深入了解 UVRPC 的设计原则
- [API 参考](docs/API_REFERENCE.md) - 完整的 API 文档
- [构建和安装](docs/BUILD_AND_INSTALL.md) - 详细的构建指南
- [迁移指南](docs/MIGRATION_GUIDE.md) - 从旧版本迁移

## 许可证

MIT License