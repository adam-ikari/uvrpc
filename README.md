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

| 传输层 | 吞吐量 (ops/s) | 相对性能 | 特点 |
|--------|----------------|----------|------|
| **INPROC** | 471,163 | 3.9x | 零拷贝，同进程内最快 |
| **IPC** | 199,818 | 1.6x | Unix Domain Socket，本地进程间通信 |
| **UDP** | 133,597 | 1.1x | 无连接，高吞吐 |
| **TCP** | 121,979 | 1.0x | 可靠传输，基准 |

### 性能特点
- **INPROC**: 零拷贝同步执行，适合同进程内高性能通信
- **IPC**: 无网络开销，适合本地进程间通信，性能优于网络传输
- **UDP**: 无连接协议，适合高吞吐、可容忍丢失的场景
- **TCP**: 可靠传输，适合需要保证数据完整性的场景

### 测试配置
- 构建模式: Release (-O2 优化，无调试符号)
- 测试时长: 3 秒
- 批处理大小: 100 请求
- 成功率: 所有传输层 100%

**注意**: 实际性能取决于硬件配置、网络条件和负载场景。

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
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
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
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
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

# 运行特定传输层的测试
./benchmark/run_benchmark.sh tcp://127.0.0.1:5555 single
./benchmark/run_benchmark.sh ipc://uvrpc_ipc_test single
./benchmark/run_benchmark.sh udp://127.0.0.1:5556 single

# 查看性能报告
cat benchmark/TRANSPORT_PERFORMANCE_REPORT.md
```

### 性能测试结果

最新测试结果（Release 模式，-O2 优化）：

- **INPROC**: 471,163 ops/s - 零拷贝，同进程内通信
- **IPC**: 199,818 ops/s - Unix Domain Socket，本地进程间通信
- **UDP**: 133,597 ops/s - 无连接协议，高吞吐
- **TCP**: 121,979 ops/s - 可靠传输，基准

所有传输层在中等负载下均达到 100% 成功率。

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

- [设计哲学](docs/DESIGN_PHILOSOPHY.md) - 深入了解 UVRPC 的设计原则
- [API 参考](docs/API_REFERENCE.md) - 完整的 API 文档
- [构建和安装](docs/BUILD_AND_INSTALL.md) - 详细的构建指南
- [迁移指南](docs/MIGRATION_GUIDE.md) - 从旧版本迁移

## 许可证

MIT License