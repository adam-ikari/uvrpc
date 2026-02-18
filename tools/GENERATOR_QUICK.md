# UVRPC Code Generator

一键生成 RPC 代码的工具。

## 快速开始

### 安装

```bash
# 构建并安装
make generator-with-flatcc
make generator-install
```

### 使用

```bash
# 生成代码
uvrpc-gen schema/rpc_api.fbs -o generated

# 查看帮助
uvrpc-gen --help
```

## Schema 格式

```flatbuffers
namespace myapp;

table AddRequest {
    a: int32;
    b: int32;
}

table AddResponse {
    result: int32;
}

// Server/Client 模式
rpc_service MathService {
    Add(AddRequest):AddResponse;
}

// Broadcast 模式（服务名包含 "Broadcast"）
rpc_service NewsBroadcastService {
    PublishNews(NewsPublishRequest):NewsPublishResponse;
}
```

## 构建选项

```bash
make generator           # 标准构建（需要 FlatCC）
make generator-with-flatcc  # 打包 FlatCC（推荐）
make generator-portable  # 便携版（glibc 2.17）
make generator-static    # 静态链接（musl）
```

## 生成的文件

```
generated/
├── myapp_math_api.h              # API 头文件
├── myapp_math_server_stub.c      # 服务端实现
├── myapp_math_client.c           # 客户端实现
├── myapp_math_rpc_common.h       # 公共定义
├── myapp_math_rpc_common.c       # 公共工具
├── myapp_builder.h               # FlatBuffers builder
└── myapp_reader.h                # FlatBuffers reader
```

## 使用示例

### 服务端

```c
#include "myapp_math_api.h"

// 实现业务逻辑
uvrpc_error_t myapp_math_handle_request(const char* method_name,
                                         const void* request,
                                         uvrpc_request_t* req) {
    if (strcmp(method_name, "Add") == 0) {
        // 处理 Add 方法
        const myapp_AddRequest_table_t* add_req = myapp_AddRequest_as_root(request);
        int32_t result = add_req->a + add_req->b;
        
        // 发送响应
        flatcc_builder_t builder;
        flatcc_builder_init(&builder);
        myapp_AddResponse_start_as_root(&builder);
        myapp_AddResponse_result_add(&builder, result);
        myapp_AddResponse_end_as_root(&builder);
        
        void* buf;
        size_t size;
        flatcc_builder_copy_buffer(&builder, &buf, &size);
        uvrpc_request_send_response(req, UVRPC_OK, buf, size);
        
        flatcc_builder_clear(&builder);
        return UVRPC_OK;
    }
    return UVRPC_ERROR_NOT_IMPLEMENTED;
}

// 主函数
int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_server_t* server = myapp_math_create_server(&loop, "tcp://0.0.0.0:5555");
    myapp_math_start_server(server);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    myapp_math_stop_server(server);
    myapp_math_free_server(server);
    uv_loop_close(&loop);
    return 0;
}
```

### 客户端

```c
#include "myapp_math_api.h"

// 回调函数
void on_add_response(uvrpc_response_t* resp, void* ctx) {
    const myapp_AddResponse_table_t* add_resp = myapp_AddResponse_as_root(resp->data);
    printf("Result: %d\n", add_resp->result);
}

// 主函数
int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);
    
    uvrpc_client_t* client = myapp_math_create_client(&loop, "tcp://127.0.0.1:5555", NULL, NULL);
    
    // 调用方法
    myapp_math_Add(client, on_add_response, NULL, 10, 20);
    
    uv_run(&loop, UV_RUN_DEFAULT);
    
    myapp_math_free_client(client);
    uv_loop_close(&loop);
    return 0;
}
```

### Broadcast 模式

```c
// 发布者
#include "myapp_newsbroadcast_api.h"

uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_publisher_t* pub = myapp_newsbroadcast_create_publisher(&loop, "udp://0.0.0.0:5555");
myapp_newsbroadcast_start_publisher(pub);

// 发布消息
myapp_newsbroadcast_PublishNews(pub, callback, NULL, "标题", "内容", time(NULL), "作者");

uv_run(&loop, UV_RUN_DEFAULT);

// 订阅者
#include "myapp_newsbroadcast_api.h"

uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_subscriber_t* sub = myapp_newsbroadcast_create_subscriber(&loop, "udp://127.0.0.1:5555");
myapp_newsbroadcast_connect_subscriber(sub);

// 订阅消息
myapp_newsbroadcast_subscribe_PublishNews(sub, callback, NULL);

uv_run(&loop, UV_RUN_DEFAULT);
```

## 编译生成的代码

```bash
# 编译服务端
gcc -o server \
    myapp_math_server_stub.c \
    myapp_math_rpc_common.c \
    myapp_builder.c \
    myapp_reader.c \
    -I../generated \
    -I../include \
    -L../dist/lib \
    -luvrpc -luv -lmimalloc

# 编译客户端
gcc -o client \
    myapp_math_client.c \
    myapp_math_rpc_common.c \
    myapp_builder.c \
    myapp_reader.c \
    -I../generated \
    -I../include \
    -L../dist/lib \
    -luvrpc -luv -lmimalloc
```

## 构建类型对比

| 类型 | FlatCC | 大小 | 兼容性 | 推荐场景 |
|------|--------|------|--------|----------|
| with-flatcc | 打包 | ~30MB | 高 | 生产环境 |
| standard | 外部 | ~20MB | 中 | 开发环境 |
| portable | 外部 | ~25MB | 高 | 老系统 |
| static | 外部 | ~15MB | 最高 | 未知目标 |

## 常见问题

**Q: 需要 Python 吗？**  
A: 打包后的可执行文件不需要 Python。

**Q: 需要 FlatCC 吗？**  
A: `make generator-with-flatcc` 打包的版本不需要。

**Q: 支持哪些传输方式？**  
A: TCP、UDP、IPC、INPROC（进程内）。

**Q: 支持广播模式吗？**  
A: 支持，服务名包含 "Broadcast" 即可。

## 更多信息

- API 文档: `docs/API_GUIDE.md`
- 快速开始: `docs/QUICK_START.md`
- 设计哲学: `docs/DESIGN_PHILOSOPHY.md`