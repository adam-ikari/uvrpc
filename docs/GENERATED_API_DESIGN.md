# UVRPC 代码生成 API 设计

## 设计原则

### 核心目标
- **类型安全**：生成的代码提供编译时类型检查
- **极简使用**：用户只需调用生成的函数
- **零样板代码**：自动生成所有必要的代码
- **完全封装**：用户不接触 UVRPC/UVBus 内部实现

## 用户使用流程

### 1. 定义服务（FlatBuffers DSL）

```flatbuffers
namespace Calculator;

// RPC 服务定义
rpc_service Calculator {
    // 加法
    Add(int32 a, int32 b): int32;
    
    // 减法
    Subtract(int32 a, int32 b): int32;
    
    // 乘法
    Multiply(int32 a, int32 b): int32;
    
    // 除法
    Divide(int32 a, int32 b): int32;
}
```

### 2. 生成代码

```bash
# 运行代码生成器
python tools/rpc_dsl_generator.py schema/rpc_example.fbs
```

### 3. 服务器端使用生成的 API

```c
#include "generated/calculator_server.h"

// 1. 实现处理器（使用 uvrpc_前缀避免重名）
void uvrpc_Calculator_Add(uint32_t msgid, 
                          const int32_t* params, 
                          size_t params_size,
                          void* ctx) {
    int32_t a = params[0];
    int32_t b = params[1];
    int32_t result = a + b;
    
    // 发送响应
    uvrpc_Calculator_Add_send_response(msgid, &result, 1, ctx);
}

void uvrpc_Calculator_Subtract(uint32_t msgid,
                               const int32_t* params,
                               size_t params_size,
                               void* ctx) {
    int32_t a = params[0];
    int32_t b = params[1];
    int32_t result = a - b;
    
    uvrpc_Calculator_Subtract_send_response(msgid, &result, 1, ctx);
}

// 2. 创建并启动服务器（生成的代码自动调用这些函数）
int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建服务器（生成的代码内部会自动调用 uvrpc_Calculator_Add、uvrpc_Calculator_Subtract）
    uvrpc_Calculator_server_t* server = uvrpc_Calculator_server_create(&loop, 
                                                                       "tcp://127.0.0.1:5555");
    
    // 启动服务器
    uvrpc_Calculator_server_start(server);
    
    // 运行事件循环
    uv_run(&loop, UV_RUN_DEFAULT);
    
    // 清理
    uvrpc_Calculator_server_free(server);
    uv_loop_close(&loop);
    
    return 0;
}
```

### 4. 客户端使用生成的 API

```c
#include "generated/calculator_client.h"

// 响应回调
void on_Calculator_Add_response(uvrpc_Calculator_Add_response_t* response, void* ctx) {
    if (response->error == 0) {
        printf("Add result: %d\n", response->result);
    } else {
        printf("Add error: %d\n", response->error);
    }
}

void on_Calculator_Subtract_response(uvrpc_Calculator_Subtract_response_t* response, void* ctx) {
    if (response->error == 0) {
        printf("Subtract result: %d\n", response->result);
    } else {
        printf("Subtract error: %d\n", response->error);
    }
}

// 1. 创建客户端
int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    // 创建客户端（生成的 API）
    uvrpc_Calculator_client_t* client = uvrpc_Calculator_client_create(&loop,
                                                                       "tcp://127.0.0.1:5555");
    
    // 连接（生成的 API）
    uvrpc_Calculator_client_connect(client);
    
    // 调用 RPC（生成的 API）
    int32_t a = 10, b = 20;
    uvrpc_Calculator_client_Add(client, a, b, on_Calculator_Add_response, NULL);
    uvrpc_Calculator_client_Subtract(client, a, b, on_Calculator_Subtract_response, NULL);
    
    // 运行事件循环
    uv_run(&loop, UV_RUN_DEFAULT);
    
    // 清理
    uvrpc_Calculator_client_free(client);
    uv_loop_close(&loop);
    
    return 0;
}
```

## 生成的 API 结构

### 服务器端 API

```c
/* 生成的服务器类型 */
typedef struct uvrpc_Calculator_server uvrpc_Calculator_server_t;

/* 用户实现的处理器（uvrpc_前缀避免重名） */
void uvrpc_Calculator_Add(uint32_t msgid, const int32_t* params, size_t params_size, void* ctx);
void uvrpc_Calculator_Subtract(uint32_t msgid, const int32_t* params, size_t params_size, void* ctx);

/* 创建服务器（内部自动调用用户实现的函数） */
uvrpc_Calculator_server_t* uvrpc_Calculator_server_create(uv_loop_t* loop,
                                                              const char* address);

/* 发送响应 */
void uvrpc_Calculator_Add_send_response(uint32_t msgid,
                                        const int32_t* result,
                                        size_t result_size,
                                        void* ctx);

void uvrpc_Calculator_Subtract_send_response(uint32_t msgid,
                                             const int32_t* result,
                                             size_t result_size,
                                             void* ctx);

/* 启动/停止 */
void uvrpc_Calculator_server_start(uvrpc_Calculator_server_t* server);
void uvrpc_Calculator_server_stop(uvrpc_Calculator_server_t* server);

/* 释放 */
void uvrpc_Calculator_server_free(uvrpc_Calculator_server_t* server);
```

### 客户端 API

```c
/* 生成的客户端类型 */
typedef struct uvrpc_Calculator_client uvrpc_Calculator_client_t;

/* 创建客户端 */
uvrpc_Calculator_client_t* uvrpc_Calculator_client_create(uv_loop_t* loop,
                                                              const char* address);

/* 连接/断开 */
void uvrpc_Calculator_client_connect(uvrpc_Calculator_client_t* client);
void uvrpc_Calculator_client_disconnect(uvrpc_Calculator_client_t* client);

/* 响应类型 */
typedef struct {
    uvrpc_error_t error;
    int32_t result;
} uvrpc_Calculator_Add_response_t;

typedef struct {
    uvrpc_error_t error;
    int32_t result;
} uvrpc_Calculator_Subtract_response_t;

/* 响应回调类型 */
typedef void (*uvrpc_Calculator_Add_callback_t)(uvrpc_Calculator_Add_response_t* response,
                                               void* ctx);

typedef void (*uvrpc_Calculator_Subtract_callback_t)(uvrpc_Calculator_Subtract_response_t* response,
                                                    void* ctx);

/* RPC 调用 */
void uvrpc_Calculator_client_Add(uvrpc_Calculator_client_t* client,
                                   int32_t a,
                                   int32_t b,
                                   uvrpc_Calculator_Add_callback_t callback,
                                   void* ctx);

void uvrpc_Calculator_client_Subtract(uvrpc_Calculator_client_t* client,
                                        int32_t a,
                                        int32_t b,
                                        uvrpc_Calculator_Subtract_callback_t callback,
                                        void* ctx);

/* 释放 */
void uvrpc_Calculator_client_free(uvrpc_Calculator_client_t* client);
```

## 命名约定

### 前缀规则

所有生成的代码都使用 `uvrpc_` 前缀，避免与用户代码重名和污染命名空间：

**类型定义**：
```c
typedef struct uvrpc_{Service}_server uvrpc_{Service}_server_t;
typedef struct uvrpc_{Service}_client uvrpc_{Service}_client_t;
```

**函数名**：
```c
// 处理器
void uvrpc_{Service}_{Method}(uint32_t msgid, const {Params}*, size_t params_size, void* ctx);

// 服务器 API
uvrpc_{Service}_server_t* uvrpc_{Service}_server_create(uv_loop_t* loop, const char* address);
void uvrpc_{Service}_server_start(uvrpc_{Service}_server_t* server);
void uvrpc_{Service}_server_free(uvrpc_{Service}_server_t* server);

// 客户端 API
uvrpc_{Service}_client_t* uvrpc_{Service}_client_create(uv_loop_t* loop, const char* address);
void uvrpc_{Service}_client_connect(uvrpc_{Service}_client_t* client);
void uvrpc_{Service}_client_free(uvrpc_{Service}_client_t* client);

// RPC 调用
void uvrpc_{Service}_client_{Method}(uvrpc_{Service}_client_t* client, ...);

// 响应类型
typedef struct uvrpc_{Service}_{Method}_response uvrpc_{Service}_{Method}_response_t;

// 响应发送
void uvrpc_{Service}_{Method}_send_response(uint32_t msgid, ...);
```

### 优势

1. **避免重名**：所有生成的符号都有统一前缀
2. **命名空间隔离**：用户代码不会与生成代码冲突
3. **清晰归属**：一眼就能看出是生成的代码
4. **IDE 友好**：自动补全更容易找到生成的函数

### 示例

```c
// 用户代码
void Add(int a, int b) {  // 用户自己的函数
    return a + b;
}

// 生成的代码
void uvrpc_Calculator_Add(uint32_t msgid, const int32_t* params, size_t params_size, void* ctx) {
    // RPC 处理器
}

// 不会冲突！
```

## 代码生成器实现

### 生成器结构

```python
class RPCCodeGenerator:
    def __init__(self, schema_file):
        self.schema = self.parse_schema(schema_file)
        self.services = self.extract_services(self.schema)
    
    def generate_server_header(self, service):
        """生成服务器头文件"""
        template = """
#ifndef {SERVICE}_SERVER_H
#define {SERVICE}_SERVER_H

#include "uvrpc.h"
#include "{service}_flatbuffers.h"

#ifdef __cplusplus
extern "C" {{
#endif

/* 服务器类型 */
typedef struct {Service}_server {Service}_server_t;

/* 用户实现的处理器（固定函数名，由生成器声明） */
{HANDLER_DECLARATIONS}

/* 创建服务器 */
{Service}_server_t* {Service}_server_create(uv_loop_t* loop,
                                              const char* address);

/* 发送响应 */
{SEND_RESPONSE_FUNCTIONS}

/* 启动/停止 */
void {Service}_server_start({Service}_server_t* server);
void {Service}_server_stop({Service}_server_t* server);

/* 释放 */
void {Service}_server_free({Service}_server_t* server);

#ifdef __cplusplus
}}
#endif

#endif /* {SERVICE}_SERVER_H */
"""
        return template.format(...)
    
    def generate_client_header(self, service):
        """生成客户端头文件"""
        template = """
#ifndef {SERVICE}_CLIENT_H
#define {SERVICE}_CLIENT_H

#include "uvrpc.h"
#include "{service}_flatbuffers.h"

#ifdef __cplusplus
extern "C" {{
#endif

/* 客户端类型 */
typedef struct {Service}_client {Service}_client_t;

/* 创建客户端 */
{Service}_client_t* {Service}_client_create(uv_loop_t* loop,
                                              const char* address);

/* 连接/断开 */
void {Service}_client_connect({Service}_client_t* client);
void {Service}_client_disconnect({Service}_client_t* client);

/* 响应类型 */
{RESPONSE_TYPES}

/* 响应回调类型 */
{CALLBACK_TYPES}

/* RPC 调用 */
{CALL_FUNCTIONS}

/* 释放 */
void {Service}_client_free({Service}_client_t* client);

#ifdef __cplusplus
}}
#endif

#endif /* {SERVICE}_CLIENT_H */
"""
        return template.format(...)
    
    def generate_server_impl(self, service):
        """生成服务器实现文件"""
        template = """
#include "{service}_server.h"

/* 服务器内部结构 */
struct {Service}_server {{
    uvrpc_server_t* uvrpc_server;
    uv_loop_t* loop;
    char* address;
}};

/* 用户实现的处理器（由生成器声明，用户实现） */
extern void {EXTERNAL_HANDLER_DECLARATIONS}

/* 处理器包装（内部调用用户函数） */
{HANDLER_WRAPPERS}

/* 创建服务器 */
{Service}_server_t* {Service}_server_create(uv_loop_t* loop,
                                              const char* address) {{
    struct {Service}_server* server = uvrpc_calloc(1, sizeof(*server));
    server->loop = loop;
    server->address = uvrpc_strdup(address);
    
    /* 创建 UVRPC 服务器 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    
    server->uvrpc_server = uvrpc_server_create(config);
    uvrpc_config_free(config);
    
    /* 自动注册所有处理器（调用用户函数） */
    {AUTO_REGISTER_HANDLERS}
    
    return ({Service}_server_t*)server;
}}

/* 发送响应实现 */
{SEND_RESPONSE_IMPLEMENTATIONS}

/* 启动/停止 */
void {Service}_server_start({Service}_server_t* server) {{
    uvrpc_server_start(server->uvrpc_server);
}}

void {Service}_server_stop({Service}_server_t* server) {{
    uvrpc_server_stop(server->uvrpc_server);
}}

/* 释放 */
void {Service}_server_free({Service}_server_t* server) {{
    if (server) {{
        if (server->uvrpc_server) {{
            uvrpc_server_free(server->uvrpc_server);
        }}
        if (server->address) {{
            uvrpc_free(server->address);
        }}
        uvrpc_free(server);
    }}
}}
"""
        return template.format(...)
    
    def generate_client_impl(self, service):
        """生成客户端实现文件"""
        template = """
#include "{service}_client.h"

/* 客户端内部结构 */
struct {Service}_client {{
    uvrpc_client_t* uvrpc_client;
    uv_loop_t* loop;
    char* address;
}};

/* 创建客户端 */
{Service}_client_t* {Service}_client_create(uv_loop_t* loop,
                                              const char* address) {{
    struct {Service}_client* client = uvrpc_calloc(1, sizeof(*client));
    client->loop = loop;
    client->address = uvrpc_strdup(address);
    
    /* 创建 UVRPC 客户端 */
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, loop);
    uvrpc_config_set_address(config, address);
    
    client->uvrpc_client = uvrpc_client_create(config);
    uvrpc_config_free(config);
    
    return ({Service}_client_t*)client;
}}

/* 连接/断开 */
void {Service}_client_connect({Service}_client_t* client) {{
    uvrpc_client_connect(client->uvrpc_client);
}}

void {Service}_client_disconnect({Service}_client_t* client) {{
    uvrpc_client_disconnect(client->uvrpc_client);
}}

/* RPC 调用实现 */
{CALL_IMPLEMENTATIONS}

/* 释放 */
void {Service}_client_free({Service}_client_t* client) {{
    if (client) {{
        if (client->uvrpc_client) {{
            uvrpc_client_free(client->uvrpc_client);
        }}
        if (client->address) {{
            uvrpc_free(client->address);
        }}
        uvrpc_free(client);
    }}
}}
"""
        return template.format(...)
```

## 生成的代码特点

### 1. 完全类型安全
- 函数参数类型明确
- 编译时类型检查
- 避免运行时错误

### 2. 零样板代码
- 用户无需手写序列化代码
- 用户无需手写网络代码
- 用户无需管理回调

### 3. 极简 API
- 函数名清晰直观
- 参数类型明确
- 返回值统一

### 4. 完全封装
- 用户不接触 UVRPC API
- 用户不接触 UVBus API
- 用户不接触 libuv API

## 高级特性

### 异步回调

```c
// 支持异步处理器
void Calculator_Add_async_handler(uint32_t msgid,
                                  const int32_t* params,
                                  size_t params_size,
                                  void* ctx) {
    // 异步处理
    async_compute(params, [](int32_t result) {
        // 异步发送响应
        Calculator_Add_send_response(msgid, &result, 1, ctx);
    });
}
```

### 超时控制

```c
// 支持超时
Calculator_client_t* client = Calculator_client_create(&loop, "tcp://127.0.0.1:5555");
Calculator_client_set_timeout(client, 5000);  // 5 秒超时
```

### 重试机制

```c
// 支持重试
Calculator_client_t* client = Calculator_client_create(&loop, "tcp://127.0.0.1:5555");
Calculator_client_set_retry(client, 3);  // 重试 3 次
```

## 总结

生成的 API 设计：

1. **类型安全**：编译时检查所有类型
2. **极简使用**：用户只需调用生成的函数
3. **零样板代码**：自动生成所有代码
4. **完全封装**：用户不接触内部实现
5. **高级特性**：支持异步、超时、重试等

用户完全不需要了解 UVRPC、UVBus 或 libuv，只需使用生成的 API！