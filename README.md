# uvrpc - 基于 msgpack 和 Nanomsg 的 RPC 框架

一个极简、高性能的 RPC 框架，基于 msgpack 二进制序列化和 Nanomsg（纯 C 实现）。

## 特性

- **极简设计**：单头文件 API，仅 4-5 个核心函数
- **高性能**：零拷贝，基于 libuv 事件循环
- **纯 C 实现**：使用 Nanomsg 替代 ZeroMQ，无需 C++ 依赖
- **二进制序列化**：使用 msgpack 高效序列化
- **异步非阻塞**：完全基于事件循环模型
- **透明度高**：结构体公开，无隐藏魔法

## 依赖

- libuv (>= 1.0)
- Nanomsg (>= 1.0) - 纯 C 实现
- msgpack (mpack 实现)
- uthash

## 编译

```bash
# 克隆项目（包含所有子模块）
git clone --recursive https://github.com/your-org/uvrpc.git
cd uvrpc

# 编译
./build.sh

# 运行示例
./build/echo_server
./build/echo_client "Hello, uvrpc!"
```

## API 使用

### 服务端

```c
#include "uvrpc.h"

// 服务处理器
int my_service_handler(void* ctx,
                       const uint8_t* request_data,
                       size_t request_size,
                       uint8_t** response_data,
                       size_t* response_size) {
    // 解析请求
    // 处理业务逻辑
    // 构造响应
    return UVRPC_OK;
}

int main() {
    uv_loop_t* loop = uv_default_loop();

    // 创建服务器
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://*:5555");

    // 注册服务
    uvrpc_server_register_service(server, "my.Service", my_service_handler, NULL);

    // 启动服务器
    uvrpc_server_start(server);

    // 运行事件循环
    uv_run(loop, UV_RUN_DEFAULT);

    // 清理
    uvrpc_server_free(server);
    uv_loop_close(loop);

    return 0;
}
```

### 客户端

```c
#include "uvrpc.h"

// 响应回调
void response_callback(void* ctx, int status,
                       const uint8_t* response_data,
                       size_t response_size) {
    // 处理响应
}

int main() {
    uv_loop_t* loop = uv_default_loop();

    // 创建客户端
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:5555");

    // 调用服务
    uvrpc_client_call(client, "my.Service",
                      request_data, request_size,
                      response_callback, NULL);

    // 运行事件循环
    uv_run(loop, UV_RUN_DEFAULT);

    // 清理
    uvrpc_client_free(client);
    uv_loop_close(loop);

    return 0;
}
```

## 消息格式

RPC 消息使用 msgpack 二进制格式：

### 请求格式 (map)
```json
{
    "request_id": uint32,
    "service_id": string,
    "method_id": string,
    "request_data": binary
}
```

### 响应格式 (map)
```json
{
    "request_id": uint32,
    "status": int32,
    "error_message": string,
    "response_data": binary
}
```

### Echo 服务示例

**EchoRequest (map)**
```json
{
    "message": string,
    "timestamp": int64
}
```

**EchoResponse (map)**
```json
{
    "reply": string,
    "processed_at": int64
}
```

## 错误码

- `UVRPC_OK` (0): 成功
- `UVRPC_ERROR` (-1): 通用错误
- `UVRPC_ERROR_INVALID_PARAM` (-2): 无效参数
- `UVRPC_ERROR_NO_MEMORY` (-3): 内存不足
- `UVRPC_ERROR_SERVICE_NOT_FOUND` (-4): 服务未找到
- `UVRPC_ERROR_TIMEOUT` (-5): 超时

## 项目结构

```
uvrpc/
├── include/uvrpc.h                 # 公共头文件
├── src/
│   ├── uvrpc_internal.h            # 内部头文件
│   ├── uvrpc_server.c              # 服务端实现
│   ├── uvrpc_client.c              # 客户端实现
│   ├── msgpack_wrapper.h           # msgpack 序列化接口
│   └── msgpack_wrapper.c           # msgpack 序列化实现
├── examples/
│   ├── echo_service.yaml           # Echo 服务定义 (YAML DSL)
│   ├── echo_server.c               # 示例服务端
│   └── echo_client.c               # 示例客户端
├── docs/
│   └── YAML_RPC_DSL.md             # YAML DSL 文档
├── deps/                           # 依赖（git submodules）
│   ├── libuv/
│   ├── zeromq/
│   ├── msgpack-c/                  # mpack 实现
│   ├── uvzmq/
│   └── uthash/
├── CMakeLists.txt
├── build.sh
└── README.md
```

## 设计哲学

1. **极简主义**：最小化 API，减少学习成本
2. **性能驱动**：零拷贝，批量处理，事件驱动
3. **透明度优先**：结构体公开，用户可完全控制
4. **用户体验至上**：清晰的错误信息，简单的使用流程

## 许可证

MIT License

## 作者

基于 uvzmq 和 FlatBuffers 开发