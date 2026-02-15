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
**服务端（代码生成模式）**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

// 2. 创建服务器（自动生成服务端代码，包含处理器注册）
Calculator_server_t* server = Calculator_server_create(config);

// 3. 启动并运行
Calculator_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);
```

**服务端（通用模式，利于 loop 复用）**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

// 2. 创建服务器（通用 API，支持多服务复用 loop）
uvrpc_server_t* server = uvrpc_server_create(config);

// 3. 注册处理器
uvrpc_server_register(server, "Calculator.Add", calculator_add_handler, NULL);
uvrpc_server_register(server, "Calculator.Subtract", calculator_subtract_handler, NULL);

// 4. 启动并运行
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);
```

**处理器自动生成**：
```flatbuffers
// 在 FlatBuffers schema 中声明服务
namespace Example;

table AddRequest {
  a: int;
  b: int;
}

table AddResponse {
  result: int;
}

rpc_service Calculator {
  Add(AddRequest): AddResponse;
}
```

```c
// 自动生成的代码（无需手写）
Calculator_Add(client, request, response_callback, ctx);
```

**客户端（代码生成模式）**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);

// 2. 创建客户端（自动生成客户端代码）
Calculator_client_t* client = Calculator_client_create(config);

// 3. 调用 RPC（自动生成的类型安全函数）
Calculator_Add(client, request, response_callback, ctx);

// 4. 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);
```

**客户端（通用模式，利于 loop 复用）**：
```c
// 1. 创建配置
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_performance_mode(config, UVRPC_PERF_LOW_LATENCY);

// 2. 创建客户端（通用 API，支持多服务复用 loop）
uvrpc_client_t* client = uvrpc_client_create(config);

// 3. 连接并调用
uvrpc_client_connect(client);
uvrpc_client_call(client, "Calculator.Add", params, size, callback, NULL);

// 4. 运行事件循环
uv_run(&loop, UV_RUN_DEFAULT);
```

#### 低学习成本
- **单头文件**：只需包含 `uvrpc.h` 即可使用所有功能
- **代码生成**：使用 FlatBuffers DSL 声明服务，自动生成类型安全的 API
- **自动生成处理器**：服务端处理器和客户端调用代码自动生成，无需手写
- **类型安全**：生成的代码提供编译时类型检查，避免运行时错误

### 2. 零线程，零锁，零全局变量

UVRPC 基于 libuv 事件循环，所有 I/O 操作都在单线程中异步执行：

- **零线程**：不创建额外线程，所有操作在事件循环中完成
- **零锁**：单线程模型，无需锁机制
- **零全局变量**：所有状态通过上下文传递，支持多实例

#### 全局变量策略

UVRPC 的"零全局变量"原则分为两个层次：

**用户层面**：
- **完全不占用**：UVRPC 不占用 `loop->data`，用户可以自由使用
- **多实例支持**：同一进程可以创建多个独立的 UVRPC 实例
- **灵活的事件循环**：多个实例可以在多个 loop 中独立运行，也可以共享同一个 loop
- **线程安全**：每个实例在自己的事件循环中运行，无竞争

**实现层面**：
- **INPROC 传输**：使用内部全局端点列表（仅在 INPROC 模式下）
- **内存分配器**：使用全局分配器类型（可配置）
- **设计原则**：仅在没有更好方案时使用全局变量，且不影响用户代码

**INPROC 传输的特殊设计**：

INPROC 是唯一使用全局变量的传输层，因为：
- 进程内通信需要全局端点注册表
- 使用链表而非 uthash，避免复杂依赖
- 全局列表仅用于端点查找，不影响用户代码

```c
// INPROC 内部实现（用户不可见）
static inproc_endpoint_t* g_endpoint_list = NULL;

// 用户代码不受影响
uv_loop_t loop1;
uv_loop_init(&loop1);
uvrpc_config_t* config1 = uvrpc_config_new();
uvrpc_config_set_loop(config1, &loop1);  // loop->data 完全由用户控制
uvrpc_server_t* server1 = uvrpc_server_create(config1);

uv_loop_t loop2;
uv_loop_init(&loop2);
uvrpc_config_t* config2 = uvrpc_config_new();
uvrpc_config_set_loop(config2, &loop2);  // loop->data 完全由用户控制
uvrpc_server_t* server2 = uvrpc_server_create(config2);
```

### 3. 性能驱动

- **零拷贝**：FlatBuffers 序列化，数据直接访问
- **高效分配**：默认使用 mimalloc 分配器
- **批量处理**：支持批量消息发送
- **事件驱动**：非阻塞 I/O，高并发处理

### 4. 透明度优先

- **公开结构体**：所有结构体定义公开，用户可直接访问
- **无隐藏魔法**：没有内部状态或隐藏逻辑
- **完全控制**：用户可完全控制对象生命周期

### 5. 循环注入与 Loop 复用

支持自定义 libuv event loop，提供灵活的多实例部署：

#### 循环注入模式
```c
uv_loop_t custom_loop;
uv_loop_init(&custom_loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &custom_loop);

uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_start(server);

uv_run(&custom_loop, UV_RUN_DEFAULT);
```

#### Loop 复用策略

**代码生成模式的改进**：
- 每个服务生成独立的函数前缀（如 `uvrpc_MathService_create_server`）
- 不同服务可以共享同一个 loop，无函数签名冲突
- 支持动态注册多个服务到同一个 loop
- 适合多服务场景和 loop 复用

**通用 API 模式的优势**：
- 使用通用的 `uvrpc_server_t` 和 `uvrpc_client_t` 类型
- 多个服务可以共享同一个 loop
- 支持动态注册和注销处理器
- 适合需要精细控制的多服务场景

**Loop 复用示例（代码生成模式）**：
```c
/* 多服务共享同一个 loop */
uv_loop_t loop;
uv_loop_init(&loop);

/* 服务 1：Calculator */
uvrpc_server_t* math_server = uvrpc_mathservice_create_server(&loop, "tcp://127.0.0.1:5555");
uvrpc_mathservice_start_server(math_server);

/* 服务 2：Echo */
uvrpc_server_t* echo_server = uvrpc_echoservice_create_server(&loop, "tcp://127.0.0.1:5556");
uvrpc_echoservice_start_server(echo_server);

/* 服务 3：User */
uvrpc_server_t* user_server = uvrpc_userservice_create_server(&loop, "tcp://127.0.0.1:5557");
uvrpc_userservice_start_server(user_server);

/* 所有服务在同一个 loop 中运行 */
uv_run(&loop, UV_RUN_DEFAULT);

/* 清理 */
uvrpc_mathservice_free_server(math_server);
uvrpc_echoservice_free_server(echo_server);
uvrpc_userservice_free_server(user_server);
```

**Loop 复用示例（通用 API 模式）**：
```c
/* 多服务共享同一个 loop */
uv_loop_t loop;
uv_loop_init(&loop);

/* 服务 1：Calculator */
uvrpc_config_t* config1 = uvrpc_config_new();
uvrpc_config_set_loop(config1, &loop);
uvrpc_config_set_address(config1, "tcp://127.0.0.1:5555");
uvrpc_server_t* server1 = uvrpc_server_create(config1);
uvrpc_server_register(server1, "Calculator.Add", calc_add_handler, NULL);
uvrpc_server_register(server1, "Calculator.Subtract", calc_subtract_handler, NULL);
uvrpc_server_start(server1);

/* 服务 2：Echo */
uvrpc_config_t* config2 = uvrpc_config_new();
uvrpc_config_set_loop(config2, &loop);
uvrpc_config_set_address(config2, "tcp://127.0.0.1:5556");
uvrpc_server_t* server2 = uvrpc_server_create(config2);
uvrpc_server_register(server2, "Echo.EchoString", echo_handler, NULL);
uvrpc_server_start(server2);

/* 所有服务在同一个 loop 中运行 */
uv_run(&loop, UV_RUN_DEFAULT);
```

**优势**：
- **多实例支持**：同一进程可创建多个独立 UVRPC 实例
- **灵活部署**：可选择独立运行或共享事件循环
- **单元测试友好**：每个测试可使用独立的事件循环
- **云原生兼容**：适合容器化部署和微服务架构
- **资源优化**：多服务共享 loop，减少线程创建开销

### 6. 异步统一

所有操作都是异步的：

- 服务端：通过回调处理请求
- 客户端：通过回调接收响应
- 连接：异步建立，通过回调通知

### 7. 多协议支持与统一抽象

支持多种传输协议，使用统一的抽象接口，使用方式完全相同：

```c
/* TCP 传输 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);

/* UDP 传输 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "udp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);

/* IPC 传输 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "ipc:///tmp/uvrpc.sock");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);

/* INPROC 传输 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "inproc://my_service");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
```

**统一的调用方式**：
- 所有传输协议使用相同的 API 调用
- 服务端：`uvrpc_server_create()`, `uvrpc_server_start()`
- 客户端：`uvrpc_client_create()`, `uvrpc_client_connect()`, `Calculator_Add()`（自动生成）
- 回调模式：连接回调、接收回调、响应回调
- **仅需修改 URL**：更换传输协议只需修改地址前缀

**代码生成带来的便利**：
- **无需手写处理器注册**：自动生成服务端处理器代码
- **无需手写客户端调用**：自动生成类型安全的客户端调用函数
- **编译时类型检查**：参数类型、返回类型自动验证
- **减少样板代码**：自动生成序列化/反序列化代码

## 极简设计的体现

### 两种使用模式

UVRPC 提供两种使用模式，满足不同场景需求：

**模式一：代码生成模式（快速开发）**
- 服务端：`<Service>_server_create()` 自动生成完整的服务端代码
- 客户端：`<Service>_client_create()` 自动生成完整的客户端代码
- 优势：快速开发，类型安全，减少样板代码
- 场景：单服务项目，快速原型开发

**模式二：通用 API 模式（灵活复用）**
- 服务端：`uvrpc_server_create()` + `uvrpc_server_register()` 手动注册处理器
- 客户端：`uvrpc_client_create()` + `uvrpc_client_call()` 手动指定方法名
- 优势：loop 复用，多服务共享，灵活控制
- 场景：多服务项目，loop 复用，精细控制

### API 最小化

**通用 API（约 10 个函数）**：
- `uvrpc_config_new()`, `uvrpc_config_free()`
- `uvrpc_config_set_loop()`, `uvrpc_config_set_address()`
- `uvrpc_config_set_transport()`, `uvrpc_config_set_comm_type()`
- `uvrpc_server_create()`, `uvrpc_server_start()`, `uvrpc_server_free()`
- `uvrpc_server_register()`
- `uvrpc_client_create()`, `uvrpc_client_connect()`, `uvrpc_client_free()`
- `uvrpc_client_call()`

**代码生成 API（自动生成）**：
- 每个服务自动生成：`<Service>_server_create()`, `<Service>_server_start()`, `<Service>_client_create()`
- 自动生成类型安全的调用函数：`<Service>_<Method>()`
- 无需手动注册处理器和指定方法名

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
│  Layer 1: 传输层（统一抽象）                          │
│  - uvrpc_transport_t (统一接口)                      │
│  - TCP 实现 (uvrpc_transport_tcp_new)               │
│  - UDP 实现 (uvrpc_transport_udp_new)               │
│  - IPC 实现 (uvrpc_transport_ipc_new)               │
│  - INPROC 实现 (uvrpc_transport_inproc_new)         │
│  - 相同的 vtable 接口                                │
├─────────────────────────────────────────────────────┤
│  Layer 0: 核心库层                                    │
│  - libuv                                            │
│  - FlatBuffers                                      │
│  - mimalloc                                         │
└─────────────────────────────────────────────────────┘
```

### 统一的传输抽象

**传输接口 (uvrpc_transport_vtable_t)**：
```c
struct uvrpc_transport_vtable {
    /* Server operations */
    int (*listen)(void* impl, const char* address,
                  uvrpc_recv_callback_t recv_cb, void* ctx);
    
    /* Client operations */
    int (*connect)(void* impl, const char* address,
                   uvrpc_connect_callback_t connect_cb,
                   uvrpc_recv_callback_t recv_cb, void* ctx);
    void (*disconnect)(void* impl);
    
    /* Send operations */
    void (*send)(void* impl, const uint8_t* data, size_t size);
    void (*send_to)(void* impl, const uint8_t* data, size_t size, void* target);
    
    /* Cleanup */
    void (*free)(void* impl);
    
    /* Optional: transport-specific operations */
    int (*set_timeout)(void* impl, uint64_t timeout_ms);
};
```

**统一的使用方式**：
```c
/* 无论使用哪种传输协议，API 调用完全相同 */

/* 1. 创建配置 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);

/* 2. 设置传输类型和地址（仅此处不同） */
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);    // 或 UDP/IPC/INPROC
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");   // 或 udp:// /ipc:// /inproc://

/* 3. 创建服务器/客户端（完全相同） */
uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_client_t* client = uvrpc_client_create(config);

/* 4. 注册处理器/连接（完全相同） */
uvrpc_server_register(server, "method", handler, NULL);
uvrpc_client_connect(client);

/* 5. 调用 RPC（完全相同） */
uvrpc_client_call(client, "method", params, size, callback, NULL);
```

**传输协议切换**：
```c
/* 只需修改这两行，其他代码无需改变 */

/* 从 TCP 切换到 UDP */
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
uvrpc_config_set_address(config, "udp://127.0.0.1:5555");

/* 从 UDP 切换到 INPROC */
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);
uvrpc_config_set_address(config, "inproc://my_service");
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

### INPROC 传输架构

#### 设计目标

INPROC (In-Process) 传输为进程内通信提供最高性能：

- **零拷贝**：数据直接在内存中传递，无需序列化
- **零网络开销**：无 TCP/IP 协议栈开销
- **零延迟**：直接函数调用级别延迟

#### 架构实现

```
进程内通信架构：

┌─────────────────────────────────────────────────────┐
│  Server Process                                       │
│                                                       │
│  ┌─────────────────────────────────────────────────┐│
│  │  uvrpc_server_t                                 ││
│  │  - transport: uvrpc_transport_t                 ││
│  │    - impl: uvrpc_inproc_transport_t            ││
│  │      - endpoint: inproc_endpoint_t             ││
│  │        - name: "test_endpoint"                 ││
│  │        - server_transport: ← 指向自己         ││
│  │        - clients: [client1, client2, ...]      ││
│  │        - client_count: 2                       ││
│  └─────────────────────────────────────────────────┘│
│                           │                          │
│                           │ 直接调用                  │
│                           ↓                          │
│  ┌─────────────────────────────────────────────────┐│
│  │  inproc_endpoint_t                             ││
│  │  - 全局端点注册表 (g_endpoint_list)            ││
│  │  - 链表结构，支持多个端点                      ││
│  └─────────────────────────────────────────────────┘│
│                           ↑                          │
│                           │ 直接调用                  │
│                           │                          │
│  ┌─────────────────────────────────────────────────┐│
│  │  uvrpc_client_t                                 ││
│  │  - transport: uvrpc_transport_t                 ││
│  │    - impl: uvrpc_inproc_transport_t            ││
│  │      - recv_cb: client_recv_callback            ││
│  │      - endpoint: → 指向同一端点                ││
│  └─────────────────────────────────────────────────┘│
│                                                       │
└─────────────────────────────────────────────────────┘
```

#### 端点管理

**端点结构**：
```c
struct inproc_endpoint {
    char* name;                        // 端点名称
    void* server_transport;            // 服务器传输引用
    void** clients;                    // 客户端列表
    int client_count;                  // 客户端数量
    int client_capacity;               // 客户端容量
    struct inproc_endpoint* next;      // 链表指针
};
```

**端点查找**：
- 使用链表结构（而非 uthash）
- 线性查找（端点数量少时性能可接受）
- 支持多端点并发存在

**全局端点列表**：
```c
static inproc_endpoint_t* g_endpoint_list = NULL;
```

**为什么使用全局列表**：
- 进程内通信需要全局注册表
- 用户不可见，不影响 API 设计
- 仅用于端点查找，不存储用户数据

#### 通信流程

**服务器启动**：
```c
// 1. 创建传输
uvrpc_transport_t* transport = uvrpc_transport_server_new(loop, UVRPC_TRANSPORT_INPROC);

// 2. 监听端点
uvrpc_transport_listen(transport, "inproc://test_endpoint", recv_cb, ctx);
   ↓
// 内部步骤：
// a. 创建端点
endpoint = uvrpc_inproc_create_endpoint("test_endpoint");

// b. 设置服务器引用
endpoint->server_transport = transport->impl;

// c. 添加到全局列表
inproc_add_endpoint(endpoint);
```

**客户端连接**：
```c
// 1. 创建传输
uvrpc_transport_t* transport = uvrpc_transport_client_new(loop, UVRPC_TRANSPORT_INPROC);

// 2. 连接端点
uvrpc_transport_connect(transport, "inproc://test_endpoint", connect_cb, recv_cb, ctx);
   ↓
// 内部步骤：
// a. 查找端点
endpoint = inproc_find_endpoint("test_endpoint");

// b. 添加客户端到端点
inproc_add_client(endpoint, transport->impl);

// c. 复制回调
transport->impl->recv_cb = recv_cb;
transport->impl->ctx = ctx;
```

**发送请求**：
```c
// 客户端发送
uvrpc_transport_send(client_transport, data, size);
   ↓
// 内部步骤：
// a. 获取端点
endpoint = client->impl->endpoint;

// b. 查找服务器
server = endpoint->server_transport;

// c. 调用服务器接收回调
server->recv_cb(data, size, server->ctx);
```

**发送响应**：
```c
// 服务器发送
uvrpc_transport_send(server_transport, data, size);
   ↓
// 内部步骤：
// a. 获取端点
endpoint = server->impl->endpoint;

// b. 遍历客户端列表
for (int i = 0; i < endpoint->client_count; i++) {
    uvrpc_inproc_transport_t* client = endpoint->clients[i];
    
    // c. 调用每个客户端的接收回调
    client->recv_cb(data, size, client->ctx);
}
```

#### 内存管理

**端点生命周期**：
- 服务器启动时创建
- 服务器停止时释放
- 客户端连接/断开不影响端点

**客户端列表管理**：
- 动态扩容（初始 4，翻倍增长）
- 客户端断开时移除
- 服务器停止时清理所有客户端

**回调复制**：
- 连接时从传输层复制到 INPROC 实现
- 避免传输层被释放后访问无效指针
- 确保回调总是指向有效内存

#### 性能特性

**零拷贝**：
- 数据指针直接传递
- 无序列化/反序列化开销
- 直接函数调用

**零延迟**：
- 无网络栈开销
- 无系统调用（除必要的异步回调）
- 无上下文切换

**高吞吐**：
- 批量发送支持
- 环形缓冲区优化
- 无锁设计

#### 使用示例

```c
// 服务器
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "inproc://my_endpoint");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);

uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "add", add_handler, NULL);
uvrpc_server_start(server);

uv_run(&loop, UV_RUN_DEFAULT);

// 客户端（同一进程）
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "inproc://my_endpoint");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_INPROC);

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);

uvrpc_client_call(client, "add", params, size, callback, NULL);

uv_run(&loop, UV_RUN_DEFAULT);
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

> **注意**：自定义传输、序列化和分配器是扩展功能，仅建议高级用户在必要时使用。大多数应用场景下，UVRPC 提供的默认实现（TCP/UDP/IPC/INPROC 传输、FlatBuffers 序列化、mimalloc 分配器）已经足够。

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

### INPROC 使用

1. 适用于同一进程内的模块通信
2. 性能最优，延迟最低
3. 不涉及网络栈，无需序列化
4. 适用于高频调用场景

## 总结

UVRPC 的设计哲学强调：

- **极简设计**：最小化 API、依赖和配置
- **简单性**：统一的编程模型，直观的使用流程
- **性能**：零拷贝、高效分配、事件驱动
- **灵活性**：循环注入、多协议、自定义扩展
- **可靠性**：错误处理、资源管理、异步保证
- **零全局变量**：用户层面完全无全局变量，支持多实例
- **类型安全**：FlatBuffers DSL 生成类型安全的 API，自动生成处理器和调用代码
- **统一抽象**：多协议使用统一接口，仅需修改 URL 即可切换传输
- **灵活部署**：多实例可独立运行或共享事件循环
- **代码生成**：服务端处理器和客户端调用代码自动生成，无需手写

这些原则使 UVRPC 成为一个高性能、易用、灵活的 RPC 框架，适合各种应用场景。

INPROC 传输作为唯一使用内部全局变量的实现，通过精心设计确保：
- 不影响用户代码
- 不占用 `loop->data`
- 支持多实例并发
- 提供最优性能