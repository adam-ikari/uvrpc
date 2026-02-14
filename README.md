# UVRPC - Ultra-Fast RPC Framework

一个极简、高性能的 RPC 框架，基于 libuv 事件循环和 FlatBuffers 序列化。

## 设计哲学

### 核心原则

1. **极简设计** - 最小化 API、依赖和配置，零学习成本
2. **零线程，零锁，零全局变量** - 所有 I/O 由 libuv 事件循环管理
3. **性能驱动** - 零拷贝、事件驱动、高性能内存分配
4. **透明度优先** - 结构体公开，用户可完全控制
5. **循环注入** - 支持自定义 libuv loop，多实例、单元测试、云原生
6. **异步统一** - 所有调用都是异步的，无同步阻塞模式
7. **多协议支持** - TCP、UDP、IPC、INPROC 传输层

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
- **循环注入**：支持自定义 libuv loop，云原生友好
- **内存分配器**：支持 mimalloc、系统分配器、自定义分配器
- **类型安全**：FlatBuffers 强类型序列化
- **单线程模型**：无锁设计，避免临界区通信

## 性能指标

| 指标 | 说明 |
|-----|------|
| 吞吐量 | 取决于具体测试场景和硬件配置 |
| 延迟 | 取决于具体测试场景和硬件配置 |
| 传输方式 | TCP / UDP / IPC / INPROC |
| 序列化 | FlatBuffers (二进制) |
| 内存分配器 | mimalloc (默认) |

**注意**：以上性能指标为理论值，实际性能请运行基准测试获取准确数据。

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

## 传输类型

### TCP
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
```

### UDP
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
uvrpc_config_set_address(config, "udp://127.0.0.1:5555");
```

### IPC
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
uvrpc_config_set_address(config, "ipc:///tmp/uvrpc.sock");
```

### INPROC
```c
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
uvrpc_config_set_address(config, "inproc://service");
```

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
# 运行基准测试获取实际性能数据
./dist/bin/uvrpc_benchmark

# 查看帮助信息
./dist/bin/uvrpc_benchmark --help
```

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