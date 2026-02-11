# UVRPC API 设计哲学

## 概述

UVRPC 是一个完整的RPC解决方案，通过DSL和代码生成工具实现100%自动化开发，用户只需要配置参数和注入uv_loop（可选）。所有调用都是异步的，业务逻辑从生成的代码中完全分离，用户无需修改生成的代码。

## UVRPC 完整架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        UVRPC 完整解决方案                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │
│  │  DSL 语法     │  │  代码生成器   │  │   C 核心库   │        │
│  │ (.uvrpc)     │  │ (Node.js)     │  │  (libuvrpc)   │        │
│  └──────────────┘  └──────────────┘  └──────────────┘        │
│         │                   │                   │              │
│         └───────────────────┴───────────────────┘              │
│                              │                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                  生成的服务框架代码（只读）                  │  │
│  │  (服务端 + 客户端 + 配置 + main函数)                      │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                用户业务逻辑文件（用户创建）                 │  │
│  │                - 不修改生成的代码                          │  │
│  │                - 通过注册函数连接                          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                     编译链接                               │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                     可执行程序                              │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

## 核心组件

### 1. DSL 语法 (.uvrpc)

YAML格式的服务定义语言，描述完整的RPC服务：

```yaml
service EchoService:
  version: "1.0"
  
  # 传输配置
  transport:
    type: tcp
    address: "0.0.0.0:5555"
    mode: point_to_point  # 或 broadcast
  
  # 性能配置
  performance:
    mode: balanced
  
  # 服务方法
  methods:
    - name: echo
      input:
        type: string
      output:
        type: string
```

### 2. 代码生成器 (Node.js)

生成完整的服务框架代码（只读）：

```bash
# 生成完整服务框架
node tools/generator.js echo.uvrpc
```

生成的文件：
- `echo_service.h` - 服务端头文件（只读）
- `echo_service.c` - 服务端框架实现（只读，含main函数）
- `echo_client.h` - 客户端头文件（只读）
- `echo_client.c` - 客户端框架实现（只读，含main函数）
- `echo_handlers.h` - 业务逻辑头文件（生成的模板）
- `Makefile` - 构建文件（只读）
- `echo_service.c` - 服务框架实现（只读，含main函数）
- `echo_client.h` - 客户端类型定义（只读）
- `echo_client.c` - 客户端框架实现（只读，含main函数）
- `Makefile` - 构建文件（只读）

### 3. 业务逻辑文件

**生成文件：**
- `echo_handlers.h` - 业务逻辑头文件（代码生成器生成）

**用户实现：**
- `echo_handlers.c` - 用户创建的业务逻辑实现
- `main.c` - 用户创建的主程序（可选，可使用生成的main）

### 4. C 核心库 (libuvrpc)

高性能RPC运行时库，提供底层支持。

## 分层架构

```
┌─────────────────────────────────────────────────────┐
│  Layer 4: DSL & 代码生成层                             │
│  ┌─────────────────────────────────────────────────┐  │
│  │  .uvrpc DSL 定义                              │  │
│  │  - 服务接口定义                                │  │
│  │  - 传输配置                                    │  │
│  │  - 性能配置                                    │  │
│  ├─────────────────────────────────────────────────┤  │
│  │  代码生成器                                    │  │
│  │  - 解析DSL                                     │  │
│  │  - 生成完整服务框架（只读）                     │  │
│  │  - 生成完整客户端框架（只读）                   │  │
│  │  - 生成handler注册API                          │  │
│  │  - 生成Makefile                                │  │
│  └─────────────────────────────────────────────────┘  │
│  适用：开发人员定义服务接口                          │
├─────────────────────────────────────────────────────┤
│  Layer 3: 生成代码层（只读）                            │
│  ┌─────────────────────────────────────────────────┐  │
│  │  生成的服务框架代码（只读）                      │  │
│  │  - 类型定义                                      │  │
│  │  - 服务处理器框架                                │  │
│  │  - 序列化/反序列化                                │  │
│  │  - handler注册API                               │  │
│  │  - 异步处理机制                                  │  │
│  │  - main函数（默认配置）                          │  │
│  ├─────────────────────────────────────────────────┤  │
│  │  生成的客户端框架代码（只读）                     │  │
│  │  - 类型定义                                      │  │
│  │  - 异步调用客户端                                │  │
│  │  - main函数（默认配置）                          │  │
│  └─────────────────────────────────────────────────┘  │
│  适用：用户无需修改，只读使用                          │
├─────────────────────────────────────────────────────┤
│  Layer 2: 业务逻辑层（用户创建）                         │
│  ┌─────────────────────────────────────────────────┐  │
│  │  业务逻辑文件                                   │  │
│  │  - echo_handlers.h (生成)                     │  │
│  │  - echo_handlers.c (用户实现)                  │  │
│  │  - 实现handler函数                             │  │
│  │  - 通过注册API连接到生成的代码                   │  │
│  │  - 完全独立的代码，不受生成影响                  │  │
│  └─────────────────────────────────────────────────┘  │
│  适用：用户实现业务逻辑                              │
├─────────────────────────────────────────────────────┤
│  Layer 1: 运行时API层（隐藏）                            │
│  ┌─────────────────────────────────────────────────┐  │
│  │  统一配置API                                     │  │
│  │  - uvrpc_config_t                             │  │
│  │  - 构建器模式                                     │  │
│  ├─────────────────────────────────────────────────┤  │
│  │  服务端API                                       │  │
│  │  - uvrpc_server_create                          │  │
│  │  - uvrpc_server_register_service               │  │
│  │  - uvrpc_server_start                            │  │
│  ├─────────────────────────────────────────────────┤  │
│  │  客户端API                                       │  │  │
│  │  - uvrpc_client_create                          │  │
│  │  - uvrpc_client_call                             │  │  │
│  │  - uvrpc_client_connect                          │  │  │
│  ├─────────────────────────────────────────────────┤  │
│  │  Async API                                         │  │
│  │  - uvrpc_async_create                           │  │
│  │  - uvrpc_async_await                             │  │  │
│  │  - uvrpc_async_await_all                         │  │
│  └─────────────────────────────────────────────────┘  │
│  适用：生成代码内部使用，用户无需直接调用              │
├─────────────────────────────────────────────────────┤
│  Layer 0: 核心库层                                      │
│  ┌─────────────────────────────────────────────────┐  │
│  │  uvrpc_core.so                                    │  │
│  │  - 事件循环集成 (libuv)                         │  │
│  │  - 网络传输 (ZMQ)                               │  │
│  │  - 序列化/反序列化 (msgpack)                      │  │
│  │  - 性能优化 (零拷贝、批量处理)                   │  │
│  └─────────────────────────────────────────────────┘  │
│  适用：底层支持                                        │
└─────────────────────────────────────────────────────┘
```

## 使用流程

### 典型开发流程

```
1. 设计服务接口 (.uvrpc)
        ↓
2. 生成服务框架 (node tools/generator.js)
        ↓
3. 创建业务逻辑文件 (handlers.c)
        ↓
4. 编译链接 (make)
        ↓
5. 运行应用
        ↓
6. 可选：注入自定义uv_loop
```

### 代码示例

#### 步骤1: 定义服务 (echo.uvrpc)

```yaml
service EchoService:
  version: "1.0"
  
  transport:
    type: inproc
    address: "inproc://echo"
    mode: point_to_point  # 或 broadcast
  
  performance:
    mode: low_latency
  
  methods:
    - name: echo
      input:
        type: string
      output:
        type: string
    
    - name: process
      input:
        type: struct
        fields:
          - name: name
            type: string
          - name: count
            type: int32
      output:
        type: struct
        fields:
          - name: result
            type: string
          - name: processed
            type: bool
```

#### 步骤2: 生成服务框架

```bash
node tools/generator.js echo.uvrpc
```

生成的文件（只读）：
- `echo_service.h` - 服务端头文件
- `echo_service.c` - 服务端框架实现
- `echo_client.h` - 客户端头文件
- `echo_client.c` - 客户端框架实现
- `Makefile` - 构建文件

#### 步骤3: 创建业务逻辑文件

代码生成器会生成 `echo_handlers.h` 头文件，用户需要创建 `echo_handlers.c` 来实现业务逻辑：

```c
#include "echo_handlers.h"

/* echo方法的handler */
int echo_service_echo_handler(
    const char* request_str,
    char** response_str
) {
    *response_str = strdup(request_str);
    return 0;
}

/* process方法的handler */
int echo_service_process_handler(
    const echo_service_process_request_t* request,
    echo_service_process_response_t* response
) {
    char result[256];
    snprintf(result, sizeof(result), "%s x%d", request->name, request->count);
    response->result = strdup(result);
    response->processed = 1;
    return 0;
}

/* 注册所有handlers */
void echo_service_register_all_handlers(uvrpc_server_t* server) {
    echo_service_register_handler(server, "echo", echo_service_echo_handler);
    echo_service_register_handler(server, "process", echo_service_process_handler);
}
```

#### 步骤4: 编译运行

```bash
make
./echo_service  # 使用生成的main函数
```

#### 步骤5: 可选 - 使用自定义main

用户也可以创建自己的main.c：

```c
#include "echo_service.h"
#include <uv.h>

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    /* 创建服务 */
    uvrpc_server_t* server = echo_service_server_create(&loop);
    
    /* 注册用户实现的handlers */
    echo_service_register_all_handlers(server);
    
    /* 启动服务 */
    echo_service_server_start(server);
    
    /* 运行事件循环 */
    uv_run(&loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    echo_service_server_free(server);
    uv_loop_close(&loop);
    
    return 0;
}
```

## 模式设计

### 异步统一

UVRPC所有调用都是异步的，没有同步调用模式：

```c
/* 服务器-客户端模式 - 异步RPC */
uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);

/* 广播模式 - 异步广播 */
uvrpc_config_set_mode(config, UVRPC_BROADCAST);
```

### 两种模式

| 模式 | ZMQ实现 | 适用场景 | 调用方式 |
|-----|---------|---------|---------|
| **SERVER_CLIENT** | ROUTER/DEALER | 服务器-客户端RPC | 异步调用，等待响应 |
| **BROADCAST** | PUB/SUB | 事件广播 | 异步发布，无需响应 |

## 分层设计原则

### Layer 4: DSL & 代码生成层

**目标：** 100%自动化代码生成

**特点：**
- ✅ 声明式接口定义
- ✅ 自动生成类型定义
- ✅ 自动生成序列化代码
- ✅ 自动生成main函数
- ✅ 自动处理uv_loop
- ✅ 默认配置管理
- ✅ 生成handler注册API

**API设计：**
- `.uvrpc` DSL文件
- Node.js 代码生成器
- 生成完整可运行的服务框架

### Layer 3: 生成代码层（只读）

**目标：** 用户无需修改生成的代码

**特点：**
- ✅ 完整的类型定义
- ✅ 服务处理器框架
- ✅ 自动序列化/反序列化
- ✅ 异步处理机制
- ✅ handler注册API
- ✅ 默认配置（可覆盖）
- ✅ 可选注入自定义uv_loop
- ✅ **完全只读，不修改**

**API设计：**
- `{service}_run()` - 使用默认配置运行
- `{service}_run_with_config()` - 使用自定义配置运行
- `{service}_run_with_loop()` - 使用自定义loop运行
- `{service}_register_handler()` - 注册handler

### Layer 2: 业务逻辑层（用户创建）

**目标：** 用户实现业务逻辑，完全独立

**特点：**
- ✅ 用户创建的文件
- ✅ 实现handler函数
- ✅ 通过注册API连接
- ✅ 不受生成代码影响
- ✅ 可以修改业务逻辑无需重新生成

**API设计：**
- `{service}_register_all_handlers()` - 用户实现，注册所有handlers

### Layer 1: 运行时API层（隐藏）

**目标：** 生成代码内部使用

**特点：**
- ✅ 用户无需直接调用
- ✅ 由生成的代码自动调用
- ✅ 提供高级用户扩展选项

### Layer 0: 核心库层

**目标：** 提供底层支持

**特点：**
- ✅ 事件驱动
- ✅ 零拷贝
- ✅ 多传输支持
- ✅ 异步处理

## 用户工作流对比

### 简单模式（推荐）

```bash
# 1. 编写DSL
cat > echo.uvrpc << 'EOF'
service EchoService:
  version: "1.0"
  transport:
    type: inproc
    mode: point_to_point
  methods:
    - name: echo
      input: { type: string }
      output: { type: string }
EOF

# 2. 生成代码
node tools/generator.js echo.uvrpc

# 3. 创建业务逻辑文件
# echo_handlers.h 由代码生成器生成，无需手动创建
cat > echo_handlers.c << 'EOF'
#include "echo_handlers.h"

int echo_service_echo_handler(const char* req, char** resp) {
    *resp = strdup(req);
    return 0;
}

void echo_service_register_all_handlers(uvrpc_server_t* server) {
    echo_service_register_handler(server, "echo", echo_service_echo_handler);
}
EOF

# 4. 编译运行
make && ./echo_service
```

### 高级模式（可选）

```c
/* 自定义main.c */
#include "echo_service.h"
#include <uv.h>

int main() {
    uv_loop_t my_loop;
    uv_loop_init(&my_loop);
    
    /* 创建服务 */
    uvrpc_server_t* server = echo_service_server_create(&my_loop);
    
    /* 注册用户实现的handlers */
    echo_service_register_all_handlers(server);
    
    /* 启动服务 */
    echo_service_server_start(server);
    
    /* 运行事件循环 */
    uv_run(&my_loop, UV_RUN_DEFAULT);
    
    /* 清理 */
    echo_service_server_free(server);
    uv_loop_close(&my_loop);
    
    return 0;
}
```

## API数量对比

| 层级 | API数量 | 主要功能 | 用户可见 | 可修改 |
|-----|---------|---------|---------|--------|
| **Layer 4** | 1个DSL | 服务定义 | ✅ | ✅ |
| **Layer 3** | ~4个/服务 | 运行API | ✅ | ❌ 只读 |
| **Layer 2** | ~2个/服务 | handler注册 | ✅ | ✅ |
| **Layer 1** | ~20个 | 运行时配置 | ❌ | ❌ |
| **Layer 0** | ~30个 | 底层API | ❌ | ❌ |
| **总计** | ~57+ | 完整解决方案 | ~7个 | - |

## 设计目标

### 1. 100%自动化 (Layer 3+4)

**目标：** 100%的工作通过DSL和代码生成完成

**实施：**
- DSL定义服务接口
- 自动生成所有样板代码
- 自动生成main函数
- 自动处理uv_loop
- 默认配置管理
- 生成handler注册API

**用户需要做的：**
- 编写DSL定义
- 创建业务逻辑文件
- 实现handler函数
- 可选：配置参数
- 可选：注入uv_loop

**用户不需要做的：**
- ❌ 修改生成的代码
- ❌ 手动编写序列化代码
- ❌ 手动管理uv_loop
- ❌ 手动编写main函数

### 2. 业务逻辑分离

**目标：** 业务逻辑完全独立，不受生成代码影响

**实施：**
- 生成的代码完全只读
- 用户在单独的文件中实现业务逻辑
- 通过注册API连接
- 修改业务逻辑无需重新生成

### 3. 异步统一

**目标：** 所有调用都是异步的

**实施：**
- 只支持异步调用
- 服务器-客户端模式：异步RPC
- 广播模式：异步广播

### 4. 高性能底层 (Layer 0)

**目标：** 所有层共享高性能底层

**实施：**
- 零拷贝
- 批量处理
- 事件驱动

## DSL完整语法

```yaml
service <服务名>:
  version: "<版本号>"
  
  # 传输配置（可选，有默认值）
  transport:
    type: inproc|ipc|tcp|udp    # 默认: inproc
    address: "<地址>"              # 默认: inproc://service
    mode: point_to_point|broadcast  # 默认: point_to_point
  
  # 性能配置（可选，有默认值）
  performance:
    mode: low_latency|balanced|high_throughput  # 默认: balanced
    batch_size: <数字>          # 可选
    io_threads: <数字>          # 可选
  
  # 服务方法
  methods:
    - name: "<方法名>"
      input:
        type: string|int32|int64|uint32|uint64|bool|struct|array
        fields:                  # struct类型需要
          - name: "<字段名>"
            type: <类型>
        item_type: <类型>        # array类型需要
      output:
        type: <类型>
        fields:                  # struct类型需要
          - name: "<字段名>"
            type: <类型>
        item_type: <类型>        # array类型需要
```

## 扩展性

### 语言支持

**当前：**
- C (主要)

**计划：**
- C++
- Rust
- Python (通过绑定)

### 传输方式

**支持的传输：**
- Inproc - 进程内通信
- IPC - 本地进程间通信
- TCP - 网络通信
- UDP - 组播/广播

**注意：** UVRPC 专注于这四种基础传输方式，不计划支持 WebSocket 或 gRPC 等高级协议。

## 最佳实践

### 1. 使用DSL定义服务

```yaml
service UserService:
  version: "1.0"
  transport:
    type: tcp
    mode: point_to_point
  methods:
    - name: getUser
      input:
        type: uint64
      output:
        type: struct
        fields:
          - name: id
            type: uint64
          - name: name
            type: string
```

### 2. 创建独立的业务逻辑文件

```c
/* user_service_handlers.c */
#include "user_service.h"

int UserService_getUser_handler(uint64_t user_id, user_t* user) {
    /* 数据库查询逻辑 */
    return query_user_from_db(user_id, user);
}

void UserService_register_all_handlers(uvrpc_server_t* server) {
    UserService_register_handler(server, "getUser", UserService_getUser_handler);
}
```

### 3. 不修改生成的代码

```bash
# 生成的代码是只读的
chmod -w user_service.h user_service.c user_client.h user_client.c
```

### 4. 直接运行生成的程序

```bash
make && ./user_service
```

## 总结

UVRPC 的分层设计哲学：

1. **Layer 4 (DSL)**：声明式服务定义
2. **Layer 3 (生成代码)**：完整可运行的服务框架（只读）
3. **Layer 2 (业务逻辑)**：用户独立的业务逻辑文件
4. **Layer 1 (运行时API)**：生成代码内部使用
5. **Layer 0 (核心库)**：高性能底层支持

这个架构实现了：
- **100%自动化**：所有样板代码自动生成
- **业务逻辑分离**：用户不修改生成的代码
- **异步统一**：所有调用都是异步的
- **零配置运行**：使用默认配置即可运行
- **可选定制**：支持自定义配置和uv_loop
- **类型安全**：自动生成类型定义
- **高性能**：底层零拷贝优化

UVRPC 是一个完整的RPC解决方案，用户只需要：
1. 编写DSL定义服务
2. 生成服务框架
3. 创建业务逻辑文件
4. 运行应用

可选地：
- 配置参数
- 注入自定义uv_loop

**关键原则：**
- ✅ 所有调用都是异步的
- ✅ 业务逻辑从生成的代码中完全分离
- ✅ 用户无需修改生成的代码
- ✅ 两种模式：服务器-客户端 和 广播
