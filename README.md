# UVRPC - Ultra-Fast RPC Framework

一个极简、高性能的 RPC 框架，基于 libuv 事件循环、ZeroMQ 传输和 msgpack 序列化。

## 设计哲学

### 核心原则

1. **极简主义** - 单头文件 API，仅核心函数，零学习成本
2. **性能驱动** - 零拷贝、批量处理、事件驱动，100k+ ops/s
3. **透明度优先** - 结构体公开，用户可完全控制，无隐藏魔法
4. **用户体验至上** - 4步完成集成，清晰的错误信息，简单的使用流程
5. **最小依赖** - 仅依赖 libuv、ZeroMQ、msgpack 和 uthash
6. **循环注入** - 支持自定义 libuv loop，多实例、单元测试、云原生
7. **异步统一** - 所有调用都是异步的，无同步阻塞模式
8. **代码生成** - DSL 驱动，100% 自动化生成序列化代码

### 架构层次

```
┌─────────────────────────────────────────────────────┐
│  Layer 4: DSL & 代码生成层                            │
│  - YAML DSL 定义服务接口                            │
│  - 自动生成类型定义和序列化代码                      │
│  - 生成完整的客户端/服务端框架                       │
├─────────────────────────────────────────────────────┤
│  Layer 3: 生成代码层（只读）                          │
│  - 完整的类型定义                                    │
│  - 序列化/反序列化实现                               │
│  - 异步客户端 API                                    │
│  - 服务处理器框架                                    │
├─────────────────────────────────────────────────────┤
│  Layer 2: 业务逻辑层（用户创建）                       │
│  - 用户实现的服务处理器                              │
│  - 完全独立，不受生成影响                             │
├─────────────────────────────────────────────────────┤
│  Layer 1: 运行时 API 层                              │
│  - 统一配置 API (uvrpc_config_t)                    │
│  - 服务端/客户端 API                                 │
│  - Async API (uvrpc_async_t)                        │
├─────────────────────────────────────────────────────┤
│  Layer 0: 核心库层                                    │
│  - 事件循环集成 (libuv)                             │
│  - 网络传输 (ZeroMQ via uvzmq)                      │
│  - 序列化 (msgpack/mpack)                           │
│  - 性能优化 (零拷贝、批量处理)                       │
└─────────────────────────────────────────────────────┘
```

详细设计哲学请参考 [docs/API_DESIGN_PHILOSOPHY.md](docs/API_DESIGN_PHILOSOPHY.md)

## 特性

- **极简设计**：单头文件 API，构建器模式配置
- **高性能**：100k+ ops/s，2ms 平均延迟
- **事件驱动**：基于 libuv，完全异步非阻塞
- **多传输支持**：TCP、INPROC、IPC（通过 ZeroMQ）
- **零拷贝**：msgpack 二进制序列化，最小化内存拷贝
- **循环注入**：支持自定义 libuv loop，云原生友好
- **代码生成**：YAML DSL 自动生成客户端/服务端代码
- **类型安全**：自动生成类型定义和序列化代码
- **单线程模型**：无锁设计，避免临界区通信

## 性能指标

| 指标 | 值 |
|-----|---|
| 吞吐量 | 100k+ ops/s |
| 平均延迟 | ~2ms |
| P99 延迟 | ~2.3ms |
| 传输方式 | TCP / INPROC / IPC |
| 序列化 | msgpack (二进制) |

## 依赖

- libuv (>= 1.0) - 事件循环
- ZeroMQ (>= 4.0) - 消息队列（通过 uvzmq）
- msgpack-c (mpack 实现) - 二进制序列化
- uthash - 哈希表
- mimalloc (可选) - 高性能内存分配器

## 编译

```bash
# 克隆项目（包含所有子模块）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 使用 CMake 编译
mkdir build && cd build
cmake ..
make

# 或使用构建脚本
./build.sh
```

## 快速开始

### 方式 1: 使用生成的代码（推荐）

#### 1. 定义服务 (YAML DSL)

```yaml
# examples/echo_service.yaml
service EchoService:
  version: "1.0"
  
  methods:
    - name: echo
      input:
        type: struct
        fields:
          - name: message
            type: string
      output:
        type: struct
        fields:
          - name: echo
            type: string
          - name: timestamp
            type: int64
```

#### 2. 生成代码

```bash
node tools/cli.js --yaml examples/echo_service.yaml --output generated
```

生成的文件：
- `generated/echoservice_gen.h` - 类型定义
- `generated/echoservice_gen.c` - 序列化实现
- `generated/echoservice_gen_client.h` - 客户端 API
- `generated/echoservice_gen_client.c` - 客户端实现
- `generated/echoservice_server_example.c` - 服务器示例
- `generated/echoservice_client_example.c` - 客户端示例

#### 3. 实现服务处理器

```c
int EchoService_echo_Handler(void* ctx,
                 const uint8_t* request_data,
                 size_t request_size,
                 uint8_t** response_data,
                 size_t* response_size) {
    (void)ctx;

    /* 反序列化请求 */
    EchoService_echo_Request_t request;
    EchoService_echo_DeserializeRequest(request_data, request_size, &request);

    /* 处理请求 */
    EchoService_echo_Response_t response;
    response.echo = strdup(request.message);
    response.timestamp = (int64_t)time(NULL);

    /* 序列化响应 */
    EchoService_echo_SerializeResponse(&response, response_data, response_size);

    /* 清理 */
    EchoService_echo_FreeRequest(&request);
    EchoService_echo_FreeResponse(&response);

    return UVRPC_OK;
}
```

#### 4. 运行示例

```bash
# 编译
make echoservice_server echoservice_client

# 运行服务器
./build/echoservice_server

# 运行客户端
./build/echoservice_client
```

### 方式 2: 使用底层 API

#### 服务端

```c
#include "uvrpc.h"

int my_handler(void* ctx,
               const uint8_t* request_data,
               size_t request_size,
               uint8_t** response_data,
               size_t* response_size) {
    /* 处理请求 */
    *response_data = malloc(response_size);
    memcpy(*response_data, request_data, response_size);
    *response_size = response_size;
    return UVRPC_OK;
}

int main() {
    uv_loop_t* loop = uv_default_loop();
    void* zmq_ctx = zmq_ctx_new();

    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
    uvrpc_config_set_zmq_ctx(config, zmq_ctx);

    /* 创建服务器 */
    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register_service(server, "myService", my_handler, NULL);
    uvrpc_server_start(server);

    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    zmq_ctx_term(zmq_ctx);
    uv_loop_close(loop);

    return 0;
}
```

#### 客户端

```c
#include "uvrpc.h"

void response_callback(void* ctx, int status,
                       const uint8_t* response_data,
                       size_t response_size) {
    /* 处理响应 */
    printf("Received response: %zu bytes\n", response_size);
}

int main() {
    uv_loop_t* loop = uv_default_loop();

    /* 创建配置 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
    uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);

    /* 创建客户端 */
    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_client_connect(client);

    /* 调用服务 */
    const char* request = "Hello, UVRPC!";
    uvrpc_client_call(client, "myService", "method",
                      (const uint8_t*)request, strlen(request),
                      response_callback, NULL);

    /* 运行事件循环 */
    uv_run(loop, UV_RUN_DEFAULT);

    /* 清理 */
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(loop);

    return 0;
}
```

## API 概览

### 配置 API (uvrpc_config_t)

```c
/* 创建配置 */
uvrpc_config_t* uvrpc_config_new(void);
void uvrpc_config_free(uvrpc_config_t* config);

/* 设置配置项 */
void uvrpc_config_set_loop(uvrpc_config_t* config, uv_loop_t* loop);
void uvrpc_config_set_address(uvrpc_config_t* config, const char* address);
void uvrpc_config_set_transport(uvrpc_config_t* config, uvrpc_transport_t transport);
void uvrpc_config_set_mode(uvrpc_config_t* config, uvrpc_mode_t mode);
void uvrpc_config_set_zmq_ctx(uvrpc_config_t* config, void* zmq_ctx);
void uvrpc_config_set_hwm(uvrpc_config_t* config, int send_hwm, int recv_hwm);
```

### 服务端 API

```c
/* 创建和释放 */
uvrpc_server_t* uvrpc_server_create(const uvrpc_config_t* config);
void uvrpc_server_free(uvrpc_server_t* server);

/* 启动和停止 */
int uvrpc_server_start(uvrpc_server_t* server);
int uvrpc_server_stop(uvrpc_server_t* server);

/* 注册服务 */
int uvrpc_server_register_service(uvrpc_server_t* server,
                                   const char* service_name,
                                   uvrpc_service_handler_t handler,
                                   void* ctx);
```

### 客户端 API

```c
/* 创建和释放 */
uvrpc_client_t* uvrpc_client_create(const uvrpc_config_t* config);
void uvrpc_client_free(uvrpc_client_t* client);

/* 连接和断开 */
int uvrpc_client_connect(uvrpc_client_t* client);
int uvrpc_client_disconnect(uvrpc_client_t* client);

/* 调用服务（异步回调） */
int uvrpc_client_call(uvrpc_client_t* client,
                       const char* service_name,
                       const char* method_name,
                       const uint8_t* request_data,
                       size_t request_size,
                       uvrpc_response_callback_t callback,
                       void* ctx);
```

### Async API

```c
/* 创建和释放 */
uvrpc_async_t* uvrpc_async_create(uv_loop_t* loop);
void uvrpc_async_free(uvrpc_async_t* async);

/* 异步调用 */
int uvrpc_client_call_async(uvrpc_client_t* client,
                             const char* service_name,
                             const char* method_name,
                             const uint8_t* request_data,
                             size_t request_size,
                             uvrpc_async_t* async);

/* 等待结果 */
const uvrpc_async_result_t* uvrpc_async_await(uvrpc_async_t* async);
const uvrpc_async_result_t* uvrpc_async_await_timeout(uvrpc_async_t* async,
                                                         uint64_t timeout_ms);
int uvrpc_async_await_all(uvrpc_async_t** asyncs, int count);
```

## 传输模式

### 传输类型

| 类型 | 说明 | 示例地址 |
|-----|------|---------|
| `UVRPC_TRANSPORT_TCP` | TCP 网络传输 | `tcp://127.0.0.1:5555` |
| `UVRPC_TRANSPORT_INPROC` | 进程内传输 | `inproc://service_name` |
| `UVRPC_TRANSPORT_IPC` | 本地进程间传输 | `ipc:///tmp/service_name.ipc` |

### 通信模式

| 模式 | ZMQ 模式 | 适用场景 |
|-----|---------|---------|
| `UVRPC_SERVER_CLIENT` | ROUTER/DEALER | 服务器-客户端 RPC |
| `UVRPC_BROADCAST` | PUB/SUB | 事件广播 |

## 消息格式

### 请求格式 (msgpack map)
```json
{
    "request_id": uint32,
    "service_id": string,
    "method_id": string,
    "request_data": binary
}
```

### 响应格式 (msgpack map)
```json
{
    "request_id": uint32,
    "status": int32,
    "error_message": string,
    "response_data": binary
}
```

## 错误码

| 错误码 | 值 | 说明 |
|-------|---|------|
| `UVRPC_OK` | 0 | 成功 |
| `UVRPC_ERROR` | -1 | 通用错误 |
| `UVRPC_ERROR_INVALID_PARAM` | -2 | 无效参数 |
| `UVRPC_ERROR_NO_MEMORY` | -3 | 内存不足 |
| `UVRPC_ERROR_SERVICE_NOT_FOUND` | -4 | 服务未找到 |
| `UVRPC_ERROR_TIMEOUT` | -5 | 超时 |

## 项目结构

```
uvrpc/
├── include/uvrpc.h                 # 公共头文件
├── src/
│   ├── uvrpc_internal.h            # 内部头文件
│   ├── uvrpc_new.c                 # 新 API 实现
│   ├── uvrpc_config.c              # 配置实现
│   ├── uvrpc_utils.c               # 工具函数
│   ├── uvzmq_impl.c                # uvzmq 集成
│   ├── msgpack_wrapper.h           # msgpack 序列化接口
│   └── msgpack_wrapper.c           # msgpack 序列化实现
├── tools/
│   ├── cli.js                      # 代码生成命令行工具
│   ├── generator.js                # 代码生成器
│   ├── parser.js                   # YAML DSL 解析器
│   └── templates/                  # 代码生成模板
│       ├── header.njk              # 头文件模板
│       ├── source.njk              # 源文件模板
│       ├── client_header.njk       # 客户端头文件模板
│       ├── client_source.njk       # 客户端源文件模板
│       └── client_example.njk      # 客户端示例模板
├── examples/
│   ├── echo_service.yaml           # Echo 服务 DSL 定义
│   ├── echo_server.c               # Echo 服务端示例
│   ├── echo_client.c               # Echo 客户端示例
│   ├── layer2_example.c            # Layer 2 API 示例
│   └── high_perf_server.c          # 高性能服务器示例
├── generated/                      # 生成的代码目录
│   ├── echoservice_gen.h           # 生成的类型定义
│   ├── echoservice_gen.c           # 生成的序列化代码
│   ├── echoservice_gen_client.h    # 生成的客户端 API
│   ├── echoservice_gen_client.c    # 生成的客户端实现
│   ├── echoservice_server_example.c # 生成的服务器示例
│   └── echoservice_client_example.c # 生成的客户端示例
├── benchmark/
│   ├── benchmark_server.c          # 基准测试服务器
│   ├── benchmark_client.c          # 基准测试客户端
│   ├── perf_test.c                 # 性能测试
│   ├── unified_test.c              # 统一测试
│   └── gen_benchmark.c             # 生成代码基准测试
├── tests/
│   ├── test_basic.c                # 基础测试
│   ├── test_performance.c          # 性能测试
│   ├── test_generated_client.c     # 生成代码客户端测试
│   └── test_new_api_perf.c         # 新 API 性能测试
├── docs/
│   ├── API_DESIGN_PHILOSOPHY.md    # API 设计哲学
│   ├── LAYER2_API_GUIDE.md         # Layer 2 API 指南
│   ├── NEW_API_DESIGN.md           # 新 API 设计
│   ├── EVENT_LOOP_MODES.md         # 事件循环模式
│   └── YAML_RPC_DSL.md             # YAML DSL 文档
├── deps/                           # 依赖（git submodules）
│   ├── libuv/                      # libuv 事件循环
│   ├── zeromq/                     # ZeroMQ 消息队列
│   ├── uvzmq/                      # libuv + ZeroMQ 集成
│   ├── msgpack-c/                  # msgpack 序列化
│   ├── uthash/                     # 哈希表
│   └── mimalloc/                   # 高性能内存分配器
├── CMakeLists.txt                  # CMake 构建配置
├── build.sh                        # 构建脚本
└── README.md                       # 本文件
```

## 文档

- [API 设计哲学](docs/API_DESIGN_PHILOSOPHY.md) - 深入了解 UVRPC 的设计原则和架构
- [Layer 2 API 指南](docs/LAYER2_API_GUIDE.md) - 使用 Layer 2 API 的详细指南
- [事件循环模式](docs/EVENT_LOOP_MODES.md) - 事件循环的不同使用模式
- [YAML RPC DSL](docs/YAML_RPC_DSL.md) - YAML DSL 语法参考

## 示例

### 基础示例

```bash
# 运行 Echo 服务端
./build/echo_server

# 运行 Echo 客户端
./build/echo_client "Hello, UVRPC!"
```

### 高性能示例

```bash
# 运行高性能服务器
./build/high_perf_server

# 运行基准测试
./build/benchmark_server &
./build/benchmark_client
```

### 生成代码示例

```bash
# 生成代码
node tools/cli.js --yaml examples/echo_service.yaml --output generated

# 编译生成的示例
make echoservice_server echoservice_client

# 运行
./build/echoservice_server
./build/echoservice_client
```

## 最佳实践

1. **使用代码生成**：对于复杂的服务接口，优先使用 YAML DSL 和代码生成
2. **注入自定义 loop**：在云原生或多实例环境中，注入自定义 libuv loop
3. **单线程模型**：避免使用临界区通信和线程安全机制
4. **异步优先**：所有调用都是异步的，避免阻塞事件循环
5. **错误处理**：检查所有返回值，使用错误码获取详细信息

## 性能优化

1. **批量处理**：使用 `uvrpc_async_await_all` 批量等待多个响应
2. **零拷贝**：序列化/反序列化最小化内存拷贝
3. **事件循环模式**：根据场景选择 `UV_RUN_DEFAULT`、`UV_RUN_ONCE` 或 `UV_RUN_NOWAIT`
4. **HWM 配置**：合理设置高水位标记，平衡内存和吞吐量

## 故障排查

### 服务器无法启动

- 检查端口是否被占用：`lsof -i :5555`
- 检查 ZMQ context 是否正确初始化
- 检查事件循环是否正确配置

### 客户端连接超时

- 检查服务器地址是否正确
- 检查防火墙设置
- 检查传输类型是否匹配（TCP/INPROC/IPC）

### 性能问题

- 使用 `UV_RUN_NOWAIT` 模式减少延迟
- 增加 HWM 提高吞吐量
- 使用 mimalloc 替换系统分配器

## 贡献

欢迎贡献代码、报告问题或提出建议。

## 许可证

MIT License

## 致谢

- libuv - 事件循环库
- ZeroMQ - 高性能消息队列
- uvzmq - libuv + ZeroMQ 集成
- msgpack - 二进制序列化
- uthash - 哈希表实现
- mimalloc - 高性能内存分配器