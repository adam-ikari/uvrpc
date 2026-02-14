# UVRPC Design Philosophy

## 核心原则

### 1. 极简设计 (Minimalist Design)

UVRPC 追求极致的简洁性，每个设计决策都遵循"少即是多"的原则：

#### 单一职责原则
- **最小化 API**：仅提供必要的函数，每个函数只做一件事
- **零冗余**：不提供重复或替代的功能
- **清晰接口**：每个函数的参数和返回值都清晰明确

#### 统一的编程模型
- **一致的命名**：`uvrpc_module_action` 格式，直观易懂
- **统一的错误处理**：所有函数返回相同风格的错误码
- **统一的回调模式**：所有异步操作使用一致的回调签名

#### 直观的使用流程
**服务端只需 4 步**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

// 2. 创建服务器
uvrpc_server_t* server = uvrpc_server_create(config);

// 3. 注册处理器
uvrpc_server_register(server, "echo", echo_handler, NULL);

// 4. 启动并运行
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);
```

**客户端只需 4 步**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);  // 可选：设置性能模式

// 2. 创建客户端
uvrpc_client_t* client = uvrpc_client_create(config);

// 3. 连接并调用
uvrpc_client_connect(client);
uvrpc_client_call(client, "method", params, size, callback, NULL);

// 4. 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);
```

#### 零学习成本
- **单头文件**：只需包含 `uvrpc.h` 即可使用所有功能
- **无需 DSL**：不需要学习专门的领域特定语言
- **无需代码生成**：直接使用 API，无需生成代码

### 2. 零线程，零锁，零全局变量

UVRPC 基于 libuv 事件循环，所有 I/O 操作都在单线程中异步执行：

- **零线程**：不创建额外线程，所有操作在事件循环中完成
- **零锁**：单线程模型，无需锁机制
- **零全局变量**：所有状态通过上下文传递，支持多实例

### 3. 性能驱动

- **零拷贝**：FlatBuffers 序列化，数据直接访问
- **高效分配**：默认使用 mimalloc 分配器
- **批量处理**：支持批量消息发送
- **事件驱动**：非阻塞 I/O，高并发处理

### 4. 透明度优先

- **公开结构体**：所有结构体定义公开，用户可直接访问
- **无隐藏魔法**：没有内部状态或隐藏逻辑
- **完全控制**：用户可完全控制对象生命周期

### 5. 循环注入

支持自定义 libuv event loop：

```c
uv_loop_t custom_loop;
uv_loop_init(&custom_loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &custom_loop);

// 运行在自定义循环中
uv_run(&custom_loop, UV_RUN_DEFAULT);
```

优势：
- 多实例支持
- 单元测试友好
- 云原生兼容

### 6. 异步统一

所有操作都是异步的：

- 服务端：通过回调处理请求
- 客户端：通过回调接收响应
- 连接：异步建立，通过回调通知

### 7. 多协议支持

支持多种传输协议：

- **TCP**：可靠的面向连接通信
- **UDP**：无连接数据报通信
- **IPC**：本地进程间通信
- **INPROC**：内存内零拷贝通信

## 极简设计的体现

### API 最小化

UVRPC 的核心 API 仅有约 20 个函数：

**配置 API (6 个)**：
- `uvrpc_config_new()`
- `uvrpc_config_free()`
- `uvrpc_config_set_loop()`
- `uvrpc_config_set_address()`
- `uvrpc_config_set_transport()`
- `uvrpc_config_set_comm_type()`

**服务端 API (4 个)**：
- `uvrpc_server_create()`
- `uvrpc_server_start()`
- `uvrpc_server_stop()`
- `uvrpc_server_free()`
- `uvrpc_server_register()`

**客户端 API (4 个)**：
- `uvrpc_client_create()`
- `uvrpc_client_connect()`
- `uvrpc_client_disconnect()`
- `uvrpc_client_free()`
- `uvrpc_client_call()`

### 数据结构最小化

所有数据结构都只包含必要的字段：

```c
// 配置结构体：仅 5 个字段
struct uvrpc_config {
    uv_loop_t* loop;
    char* address;
    uvrpc_transport_type transport;
    uvrpc_comm_type_t comm_type;
    uvrpc_perf_mode_t performance_mode;  // 性能模式
};

// 请求结构体：仅 6 个字段
struct uvrpc_request {
    uvrpc_server_t* server;
    uint32_t msgid;
    char* method;
    uint8_t* params;
    size_t params_size;
    void* user_data;
};
```

### 依赖最小化

UVRPC 仅依赖 4 个核心库：

1. **libuv**：事件循环（必需）
2. **FlatCC**：FlatBuffers 编译器（必需）
3. **mimalloc**：高性能内存分配器（可选）
4. **uthash**：哈希表（必需）

### 编译最小化

```bash
# 只需一行命令即可构建
./build.sh

# 或使用 CMake
mkdir build && cd build
cmake ..
make
```

### 配置最小化

无需复杂的配置文件，所有配置通过代码完成：

```c
// 无需配置文件，无需环境变量
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
```

## 架构设计

### 分层架构

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: 应用层                                      │
│  - 服务处理器                                        │
│  - 客户端回调                                        │
│  - 用户逻辑                                          │
├─────────────────────────────────────────────────────┤
│  Layer 2: RPC API 层                                 │
│  - uvrpc_server_t                                   │
│  - uvrpc_client_t                                   │
│  - uvrpc_publisher_t                                │
│  - uvrpc_subscriber_t                               │
├─────────────────────────────────────────────────────┤
│  Layer 1: 传输层                                     │
│  - uvrpc_transport_t                                │
│  - TCP/UDP/IPC/INPROC 实现                          │
├─────────────────────────────────────────────────────┤
│  Layer 0: 核心库层                                    │
│  - libuv                                            │
│  - FlatBuffers                                      │
│  - mimalloc                                         │
└─────────────────────────────────────────────────────┘
```

### 数据流

#### 请求-响应流程

```
客户端                              服务端
  │                                   │
  │  1. uvrpc_client_call()           │
  │     ↓                              │
  │  2. encode_request()              │
  │     ↓                              │
  │  3. transport_send()              │
  │──────────────────────────────────>│
  │                                   │ 4. transport_recv()
  │                                   │    ↓
  │                                   │ 5. decode_request()
  │                                   │    ↓
  │                                   │ 6. handler()
  │                                   │    ↓
  │                                   │ 7. encode_response()
  │                                   │    ↓
  │<──────────────────────────────────│ 8. transport_send()
  │  9. transport_recv()              │
  │     ↓                              │
  │ 10. decode_response()             │
  │     ↓                              │
  │ 11. callback()                     │
  │                                   │
```

#### 发布-订阅流程

```
发布者                              订阅者
  │                                   │
  │  1. uvrpc_publisher_publish()     │
  │     ↓                              │
  │  2. transport_send()              │
  │──────────────────────────────────>│
  │                                   │ 3. transport_recv()
  │                                   │    ↓
  │                                   │ 4. callback()
  │                                   │
```

## 内存管理

### 分配器支持

UVRPC 支持三种内存分配器：

1. **mimalloc** (默认)：高性能内存分配器
2. **system**：标准 malloc/free
3. **custom**：用户自定义分配器

### 内存分配策略

- **零拷贝**：FlatBuffers 数据直接访问，无需拷贝
- **批量分配**：减少分配次数
- **及时释放**：回调完成后立即释放

## 错误处理

### 错误码

```c
#define UVRPC_OK 0
#define UVRPC_ERROR -1
#define UVRPC_ERROR_INVALID_PARAM -2
#define UVRPC_ERROR_NO_MEMORY -3
#define UVRPC_ERROR_NOT_CONNECTED -4
#define UVRPC_ERROR_TIMEOUT -5
#define UVRPC_ERROR_TRANSPORT -6
```

### 错误处理模式

```c
int ret = uvrpc_client_connect(client);
if (ret != UVRPC_OK) {
    // 处理错误
    fprintf(stderr, "Connection failed: %d\n", ret);
}
```

## 性能优化

### 零拷贝优化

- FlatBuffers 数据直接访问
- 避免不必要的内存拷贝
- 使用指针传递数据

### 事件循环优化

- 单线程事件循环
- 非阻塞 I/O
- 批量处理消息

### 内存分配优化

- 使用 mimalloc 分配器
- 减少内存碎片
- 提高分配速度

### 性能模式

UVRPC 提供两种性能模式，可根据应用场景选择：

#### 低延迟模式 (UVRPC_PERF_LOW_LATENCY)

```c
uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);
```

**特点**：
- 立即发送每个请求
- 最小化响应时间
- 适用于实时系统

**适用场景**：
- 实时游戏
- 高频交易
- 在线服务
- 交互式应用

**典型性能**：
- 延迟：< 1ms
- 吞吐量：~118k ops/s

#### 高吞吐模式 (UVRPC_PERF_HIGH_THROUGHPUT)

```c
uvrpc_config_set_performance_mode(config, UVRPC_PERF_HIGH_THROUGHPUT);
```

**特点**：
- 允许请求批处理
- 最大化吞吐量
- 适用于批处理场景

**适用场景**：
- 批量数据处理
- 日志收集
- 数据同步
- 后台任务

**典型性能**：
- 延迟：略高
- 吞吐量：~119k ops/s

#### 环形缓冲区优化

客户端回调路由使用环形缓冲区数组，性能优势：

- **O(1) 查找**：直接数组访问，无哈希计算
- **缓存友好**：顺序内存访问，无指针跳转
- **内存高效**：83% 内存节省（24字节 vs 80字节/条目）
- **可配置**：编译期配置 `UVRPC_MAX_PENDING_CALLBACKS`

```c
// 编译期配置
#define UVRPC_MAX_PENDING_CALLBACKS 10000  // 默认
#define UVRPC_MAX_PENDING_CALLBACKS 100000  // 高并发
```

## 扩展性

### 自定义传输

通过实现 `uvrpc_transport_t` 接口可以添加自定义传输协议。

### 自定义序列化

通过修改 `uvrpc_flatbuffers.c` 可以支持其他序列化格式。

### 自定义分配器

通过实现 `uvrpc_custom_allocator_t` 接口可以使用自定义分配器。

## 最佳实践

### 服务端

1. 使用 `uvrpc_config_set_loop()` 注入自定义事件循环
2. 在 `uv_handler` 中快速处理请求，避免阻塞
3. 使用 `uvrpc_request_send_response()` 异步发送响应

### 客户端

1. 使用 `uvrpc_client_call()` 异步调用
2. 在回调中处理响应
3. 使用 `uvrpc_client_disconnect()` 断开连接

### 内存管理

1. 使用 `uvrpc_request_free()` 释放请求
2. 使用 `uvrpc_response_free()` 释放响应
3. 使用 `uvrpc_server_free()` 和 `uvrpc_client_free()` 释放对象

## 总结

UVRPC 的设计哲学强调：

- **极简设计**：最小化 API、依赖和配置，零学习成本
- **简单性**：统一的编程模型，直观的使用流程
- **性能**：零拷贝、高效分配、事件驱动
- **灵活性**：循环注入、多协议、自定义扩展
- **可靠性**：错误处理、资源管理、异步保证

这些原则使 UVRPC 成为一个高性能、易用、灵活的 RPC 框架，适合各种应用场景。