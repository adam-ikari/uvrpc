# 快速开始

5 分钟完成构建并跑通第一个 RPC。

## 前置要求

- GCC ≥ 4.8（或 Clang）
- CMake ≥ 3.15
- make
- Git

## 构建

vendored 子模块（libuv、flatcc、mimalloc、gtest）必须在 configure 之前构建好 ——
`cmake` 自己找不到 flatcc。

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh   # 拉子模块，构建 libuv/flatcc/mimalloc/gtest
./build.sh                # cmake -S . -B build + 构建（Release，mimalloc）
```

产物输出到 `dist/bin/`。

::: tip 分配器
默认构建用 mimalloc，由 `setup_deps.sh` 从子模块构建。想改用系统分配器：
`cmake -S . -B build -DUVRPC_ALLOCATOR_DEFAULT=system`，然后照常构建。这只切换分配器，
flatcc 仍然来自 `setup_deps.sh`。
:::

## 运行示例

```bash
# 终端 1 —— 启动服务端
./dist/bin/simple_server

# 终端 2 —— 运行客户端
./dist/bin/simple_client
```

看到一次请求/响应往返，就说明 UVRPC 已经跑起来了。

## 核心概念

### 传输

UVRPC 有五种传输，靠地址前缀选择，API 完全一致：

| 传输     | 地址                 | 适用场景           |
|----------|----------------------|--------------------|
| TCP      | `tcp://host:port`    | 跨机器、可靠传输   |
| UDP      | `udp://host:port`    | 高吞吐、可丢包     |
| IPC      | `ipc:///path`        | 本机跨进程         |
| INPROC   | `inproc://name`      | 进程内零拷贝；需要注册表 |
| SAMELOOP | `sameloop://name`    | 同一 loop，最快；需要注册表 |

### 配置

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_transport(config, UVBUS_TRANSPORT_TCP);
```

三个 socket 传输之间切换只改一行 —— 地址前缀和 `UVBUS_TRANSPORT_*` 常量而已。
INPROC 与 SAMELOOP 是例外：它们通过一个由调用方创建、并传给每个需要互相通信的 config 的
注册表互相找到。

```c
uvbus_loop_registry_t* reg = uvbus_loop_registry_new();
uvrpc_config_set_loop_registry(server_config, reg);
uvrpc_config_set_loop_registry(client_config, reg);
/* ... server 与 client 释放之后： */
uvbus_loop_registry_free(reg);
```

## 完整 RPC

### 服务端

```c
#include "uvrpc.h"

void add_handler(uvrpc_request_t* req, void* ctx) {
    (void)ctx;
    int32_t a = *(int32_t*)req->params;
    int32_t b = *(int32_t*)(req->params + 4);
    int32_t result = a + b;
    uvrpc_request_send_response(req, UVRPC_OK, (uint8_t*)&result, sizeof(result));
    uvrpc_request_free(req);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_config_t* cfg = uvrpc_config_new();
    uvrpc_config_set_loop(cfg, &loop);
    uvrpc_config_set_address(cfg, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_TCP);

    uvrpc_server_t* server = uvrpc_server_create(cfg);
    uvrpc_server_register(server, "add", add_handler, NULL);
    uvrpc_server_start(server);

    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_server_free(server);
    uvrpc_config_free(cfg);
    uv_loop_close(&loop);
    return 0;
}
```

### 客户端

```c
#include "uvrpc.h"

static void on_response(uvrpc_response_t* resp, void* ctx) {
    (void)ctx;
    int32_t result = *(int32_t*)resp->result;
    printf("Result: %d\n", result);
    uvrpc_response_free(resp);
}

int main(void) {
    uv_loop_t loop = {0};
    uv_loop_init(&loop);

    uvrpc_config_t* cfg = uvrpc_config_new();
    uvrpc_config_set_loop(cfg, &loop);
    uvrpc_config_set_address(cfg, "tcp://127.0.0.1:5555");
    uvrpc_config_set_transport(cfg, UVBUS_TRANSPORT_TCP);

    uvrpc_client_t* client = uvrpc_client_create(cfg);
    uvrpc_client_connect(client);

    int32_t params[2] = {10, 20};
    uvrpc_client_call(client, "add", (uint8_t*)params, sizeof(params),
                      on_response, NULL);

    uv_run(&loop, UV_RUN_DEFAULT);
    uvrpc_client_free(client);
    uvrpc_config_free(cfg);
    uv_loop_close(&loop);
    return 0;
}
```

::: warning 连接是异步的
`uvrpc_client_connect()` 立即返回，连接完成前 `uvrpc_client_call()` 会返回
`UVRPC_ERROR_NOT_CONNECTED`。要么在连接回调里再发请求
（`uvrpc_client_connect_with_callback`），要么让循环先跑起来。回调与参数的指针生命周期
规则见 `include/uvrpc.h:258-272`。
:::

## RPC 模式

客户端有三种调用模式：

```c
/* 普通请求/响应 */
uvrpc_client_call(client, "add", params, size, on_response, ctx);

/* Oneway —— 发完即走，不等响应 */
uvrpc_client_call_oneway(client, "log", params, size);

/* 流式响应 —— 服务端发多个分块 */
uvrpc_client_call(client, "stream", params, size, on_stream_response, ctx);
```

服务端中间分块用 `uvrpc_request_send_response_more`，最后一块用
`uvrpc_request_send_response`。

## 错误处理

```c
int ret = uvrpc_server_start(server);
if (ret != UVRPC_OK) {
    fprintf(stderr, "Failed to start server: %d\n", ret);
    return 1;
}
```

响应回调里检查 `resp->status`：

```c
static void on_response(uvrpc_response_t* resp, void* ctx) {
    if (resp->status != UVRPC_OK) {
        fprintf(stderr, "Request failed: %d\n", resp->status);
    }
    uvrpc_response_free(resp);
}
```

## 性能自测

```bash
./dist/bin/perf_benchmark 100000 inproc      # 顺序 ping-pong，[请求数] [传输]
./dist/bin/direct_call_benchmark             # 函数调用下限
```

没有标注主机与构建配置的吞吐数字不可信，方法与实测见
[性能测试](/zh/guide/benchmark)。

## 下一步

- [构建安装](/zh/build-install) — 完整构建选项与依赖。
- [API 指南](/zh/guide/api-guide) — 完整 API。
- [设计哲学](/zh/guide/design-philosophy) — 零线程、零锁、零可变全局。
- [示例程序](https://github.com/adam-ikari/uvrpc/tree/main/examples) — 可运行的示例。
