# UVRPC 代码生成 API 设计

## 设计原则

### 核心目标
- **类型安全**：生成的代码提供编译时类型检查
- **极简使用**：用户只需调用生成的函数
- **零样板代码**：自动生成所有必要的代码
- **完全封装**：用户不接触 UVRPC/UVBus 内部实现

## 用户使用流程

### 1. 定义服务（FlatBuffers DSL）

DSL 是普通 FlatBuffers schema 加一段 `rpc_service` 声明 —— 请求与响应必须先各自
声明成 table，服务只写"方法 → (请求表): 响应表"。下面是仓库里真实可跑的
`schema/rpc_api.fbs`（节选）：

```flatbuffers
namespace rpc;

table MathAddRequest {
    a: int32;
    b: int32;
}

table MathAddResponse {
    result: int32;
}

/* 服务声明：Method(RequestType):ResponseType; */
rpc_service MathService {
    Add(MathAddRequest):MathAddResponse;
    Subtract(MathSubtractRequest):MathSubtractResponse;
    Multiply(MathMultiplyRequest):MathMultiplyResponse;
    Divide(MathDivideRequest):MathDivideResponse;
}
```

::: warning 不是 grpc 那种内联参数表
`Add(int32 a, int32 b): int32` 这种写法**不支持**。方法签名只能引用已声明的 table，
因为生成物靠 flatcc 读写这些表；没有对应 table 的标量参数会在解析阶段被拒。
:::

### 2. 生成代码

```bash
# flatcc 由 setup_deps.sh 装到 deps/flatcc/bin
python3 tools/uvrpcc.py schema/rpc_api.fbs -o generated/ \
    --flatcc deps/flatcc/bin/flatcc
```

一个 `rpc_service` 产出五个文件（以 `MathService` 为例）：

| 文件 | 内容 |
|---|---|
| `rpc_mathservice_api.h` | 对外声明：server/client 生命周期 + 每个方法的 async 与 `_sync` 版本 |
| `rpc_mathservice_server_stub.c` | 注册给 UVRPC 的分发回调，转调你实现的 `..._handle_request` |
| `rpc_mathservice_client.c` | 每个方法的序列化 + 发送 |
| `rpc_mathservice_rpc_common.{h,c}` | 方法的请求/响应 POJO，以及 `..._all` / `..._any` 组合调用 |

外加 flatcc 自己生成的 `rpc_api_reader.h` / `rpc_api_builder.h` 与
`flatbuffers_common_{reader,builder}.h`。

### 3. 服务器端使用生成的 API

服务端只需要实现**一个函数**：`uvrpc_<service>_handle_request`。生成的 stub 已经
把帧解出来、按 `method_name` 转交给你，响应也用 UVRPC 原生对象发回。

```c
#include "generated/rpc_mathservice_api.h"

/* 声明在生成头里，必须由你实现 */
uvrpc_error_t uvrpc_mathservice_handle_request(const char* method_name,
                                              const void* request,
                                              uvrpc_request_t* req) {
    if (strcmp(method_name, "Add") == 0) {
        /* 必须用 _as_root 解引用 flatbuffer 根偏移，直接把指针当 table 用会读错位置 */
        rpc_MathAddRequest_table_t r = rpc_MathAddRequest_as_root(request);
        int32_t a = rpc_MathAddRequest_a(r);
        int32_t b = rpc_MathAddRequest_b(r);

        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        rpc_MathAddResponse_start_as_root(&builder);
        rpc_MathAddResponse_result_add(&builder, a + b);
        rpc_MathAddResponse_end_as_root(&builder);

        size_t size;
        void* buf = flatcc_builder_finalize_buffer(&builder, &size);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        flatcc_builder_free(buf);
        flatcc_builder_clear(&builder);
        return UVRPC_OK;
    }
    return UVRPC_ERROR_NOT_FOUND;
}

int main(void) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    uvrpc_server_t* server = uvrpc_mathservice_create_server(&loop, "tcp://127.0.0.1:5555", NULL);
    if (!server) return 1;
    if (uvrpc_mathservice_start_server(server) != UVRPC_OK) return 1;

    uv_run(&loop, UV_RUN_DEFAULT);

    uvrpc_mathservice_free_server(server);
    uv_loop_close(&loop);
    return 0;
}
```

### 3a. 链接哪个 .c 决定你要实现什么

链接了 `<service>_server_stub.c`，就必须实现该 service 的
`uvrpc_<service>_handle_request` —— stub 里有对它的引用，不实现会在链接期报
undefined reference。只建客户端的程序不需要实现任何 handler，所以**不要**图省事
把整个生成目录的 `.c` 全塞进链接命令行。

| 你链接的 | 你必须提供 |
| --- | --- |
| `<service>_server_stub.c` | `uvrpc_<service>_handle_request()` |
| `<service>_client.c` | 无 |
| `<service>_rpc_common.c` | 无 |

把两个 schema 的生成源合并成一个变量是很自然的做法，会让无关的 demo 也被拖进
链接错误里。按 service 挑着链即可。

### 4. 客户端使用生成的 API

每个方法给两条路：`uvrpc_<service>_<Method>`（回调式）与
`uvrpc_<service>_<Method>_sync`（内部走 `uvrpc_async`，可带超时）。

```c
#include "generated/rpc_mathservice_api.h"

/* 回调里既要发请求又要收尾，所以把 client 与 loop 一起带过去 */
typedef struct { uvrpc_client_t* client; uv_loop_t* loop; } session_t;

static void on_add(uvrpc_response_t* resp, void* ctx) {
    session_t* s = (session_t*)ctx;

    if (resp->status != UVRPC_OK) {
        printf("Add failed: %d\n", resp->status);
    } else {
        rpc_MathAddResponse_table_t r = rpc_MathAddResponse_as_root(resp->result);
        printf("Add result: %d\n", rpc_MathAddResponse_result(r));
    }
    uv_stop(s->loop);          /* 否则 UV_RUN_DEFAULT 不会返回：连接句柄仍活跃 */
}

static void on_connect(int status, void* ctx) {
    session_t* s = (session_t*)ctx;
    if (status != 0) {
        fprintf(stderr, "connect failed: %d\n", status);
        uv_stop(s->loop);
        return;
    }
    rpc_MathAddRequest_t req = { .a = 10, .b = 20 };
    uvrpc_mathservice_Add(s->client, on_add, s, &req);
}

int main(void) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    session_t s = { .loop = &loop };
    s.client = uvrpc_mathservice_create_client(&loop, "tcp://127.0.0.1:5555",
                                               NULL, on_connect, &s);
    if (!s.client) return 1;

    /* create_client 内部已发起连接；请求在 on_connect 里发出。
     * 连接是异步的，绝不要在这里抢先调用 RPC。 */
    uv_run(&loop, UV_RUN_DEFAULT);

    uvrpc_mathservice_free_client(s.client);
    uv_loop_close(&loop);
    return 0;
}
```

同步写法（`_sync` 内部用 `uvrpc_async` 驱动所属 loop，直到响应到达或 `timeout_ms` 到期）：

```c
rpc_MathAddRequest_t req = { .a = 10, .b = 20 };
rpc_MathAddResponse_t resp;
uvrpc_error_t err = uvrpc_mathservice_Add_sync(client, &resp, &req, 5000 /* ms */);
if (err == UVRPC_OK) printf("result = %d\n", resp.result);
```

## 生成的 API 结构

### 服务器端 API

```c
/* 由你实现：所有方法共用这一个入口，按 method_name 分派 */
uvrpc_error_t uvrpc_mathservice_handle_request(const char* method_name,
                                              const void* request,
                                              uvrpc_request_t* req);

/* 生命周期。句柄类型就是通用的 uvrpc_server_t，没有 per-service 类型 */
uvrpc_server_t* uvrpc_mathservice_create_server(uv_loop_t* loop, const char* address);
uvrpc_error_t   uvrpc_mathservice_start_server(uvrpc_server_t* server);
void            uvrpc_mathservice_stop_server(uvrpc_server_t* server);
void            uvrpc_mathservice_free_server(uvrpc_server_t* server);
```

### 客户端 API

```c
/* 句柄类型是通用的 uvrpc_client_t；callback/ctx 透传给
 * uvrpc_client_connect_with_callback()，可为 NULL */
uvrpc_client_t* uvrpc_mathservice_create_client(uv_loop_t* loop,
                                                const char* address,
                                                uvrpc_connect_callback_t callback,
                                                void* ctx);
void uvrpc_mathservice_free_client(uvrpc_client_t* client);

/* 请求/响应是普通结构体（在 rpc_common.h 里），不是生成的句柄类型 */
typedef struct { int32_t a; int32_t b; } rpc_MathAddRequest_t;
typedef struct { int32_t result; }       rpc_MathAddResponse_t;

/* 回调式：callback 是标准的 uvrpc_callback_t，收到的是 flatcc 缓冲区 */
uvrpc_error_t uvrpc_mathservice_Add(uvrpc_client_t* client,
                                    uvrpc_callback_t callback, void* ctx,
                                    const rpc_MathAddRequest_t* request);

/* 同步式：timeout_ms 在这里，不在任何 config 上 */
uvrpc_error_t uvrpc_mathservice_Add_sync(uvrpc_client_t* client,
                                         rpc_MathAddResponse_t* response,
                                         const rpc_MathAddRequest_t* request,
                                         uint64_t timeout_ms);
```

`rpc_common` 里另有组合调用：`uvrpc_mathservice_add_all(...)` 并发发出同一方法的多组
请求并等齐，`..._any(...)` 取最先完成者，两者也都接受 `timeout_ms`。

## 命名约定

### 前缀规则

生成符号统一带前缀，避免污染用户命名空间：

| 来源 | 生成名 | 例 |
|---|---|---|
| 服务名 | `uvrpc_<service_lower>_<verb>` | `uvrpc_mathservice_create_server` |
| 方法（异步） | `uvrpc_<service_lower>_<Method>` | `uvrpc_mathservice_Add` |
| 方法（同步） | 上一项加 `_sync` | `uvrpc_mathservice_Add_sync` |
| 请求/响应结构 | `rpc_<Table>Request_t` / `rpc_<Table>Response_t` | `rpc_MathAddRequest_t` |
| 平表读写 | flatcc 的 `rpc_<Table>_as_root` / `_a` 等 | `rpc_MathAddResponse_result(r)` |
| 文件名 | `rpc_<schema>_<service>_{api.h,client.c,server_stub.c,rpc_common.{h,c}}` | `rpc_mathservice_api.h` |

`service_lower` 是服务名整体小写（`MathService` → `mathservice`）；表名前缀取自
schema 的 `namespace`。

### 优势

1. **避免重名**：所有生成符号都有统一前缀
2. **命名空间隔离**：用户代码不会与生成代码冲突
3. **清晰归属**：一眼看出是生成的代码
4. **IDE 友好**：自动补全更容易找到生成函数
5. **不藏 UVRPC**：句柄仍是 `uvrpc_server_t` / `uvrpc_client_t`，需要时可以直接用通用
   API（例如自己调 `uvrpc_client_call_batch()`），生成层不做不透明的包装

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

回调类型是通用的 `uvrpc_callback_t`，参数是 `uvrpc_response_t*`：`resp->result`
指向 flatcc 缓冲区，用 `rpc_<Table>..._as_root()` 读取，指针只在回调期间有效。

### 超时控制

超时**只存在于 `_sync` 与 `_all` / `_any` 这些等待型包装上**，作为显式的
`timeout_ms` 入参（`0` 表示不超时）。异步回调式没有超时：RPC 层不掌握你的等待意图，
需要就自己起 `uv_timer`。`uvrpc_config_t` 上曾有 `timeout_ms` 字段，因无人读取已删除
—— 把它加回来之前请先确认你真的需要它生效。

### 重试机制

生成代码不含重试逻辑；它直接调用 `uvrpc_client_call()`，而后者只在
`uvrpc_client_set_max_retries()` 设置过值时做同步重投（同一进程内立即重试，不泵循环）。
需要带退避的重试用 `uvrpc_async_retry_with_backoff()`。

### 缓冲区归属

生成的 client.c 与仓库里的例子都用 `free()` 释放 `flatcc_builder_finalize_buffer()`
得到的缓冲区 —— 这是对的：flatcc 用的是自己的分配器（默认即 malloc），不是
`uvrpc_alloc`。因此**不要**把 flatcc 缓冲区交给 `uvrpc_free()`，在 mimalloc 或自定义
分配器构建下那是跨堆释放。

::: tip 库内部也遵守同一条规则（2026-09-29 修复）
`uvrpc_encode_request()` 等编解码函数返回的正是 flatcc 分配的缓冲区。库内部原先用
`uvrpc_free()` 释放它们 —— system 分配器构建下两者同源、看不出问题，mimalloc 与自定义
分配器构建下则是跨堆释放。现在 `src/uvrpc_client.c` 与 `src/uvrpc_server.c` 统一调用
`uvrpc_free_encoded()`（`src/uvrpc_flatbuffers.h`，内部头文件，包一层 `free()`），
`tests/allocator_ownership_test.c` 在自定义分配器构建下守住这条边界。
:::

## 总结

生成的 API 设计：

1. **类型安全**：编译时检查所有类型
2. **极简使用**：用户只需调用生成的函数
3. **零样板代码**：自动生成所有代码
4. **完全封装**：用户不接触内部实现
5. **高级特性**：支持异步、超时、重试等

用户完全不需要了解 UVRPC、UVBus 或 libuv，只需使用生成的 API！