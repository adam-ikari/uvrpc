# 使用 uvrpc DSL 生成的代码

本文档说明如何使用 uvrpc DSL 代码生成器生成的代码。

## 快速开始

### 1. 定义服务

创建 YAML 文件定义您的服务，例如 `my_service.yaml`：

```yaml
service: "MyService"
version: "1.0.0"
description: "我的服务"

methods:
  - name: "get_user"
    description: "获取用户信息"
    request:
      type: "map"
      fields:
        - name: "user_id"
          type: "int"
          required: true
          description: "用户 ID"
    response:
      type: "map"
      fields:
        - name: "id"
          type: "int"
          description: "用户 ID"
        - name: "name"
          type: "string"
          description: "用户名"
        - name: "email"
          type: "string"
          description: "邮箱"
```

### 2. 生成代码

```bash
cd tools
node cli.js --yaml ../examples/my_service.yaml --output ../generated
```

### 3. 集成到项目

生成的代码包含以下文件：

- `myservice_gen.h` - 头文件
- `myservice_gen.c` - 源文件
- `myservice_server_example.c` - 服务器示例
- `myservice_client_example.c` - 客户端示例

## 使用生成的结构体

### 请求结构体

```c
MyService_getUser_Request_t request;
memset(&request, 0, sizeof(MyService_getUser_Request_t));
request.user_id = 123;
```

### 响应结构体

```c
MyService_getUser_Response_t response;
memset(&response, 0, sizeof(MyService_getUser_Response_t));
```

## 序列化和反序列化

### 序列化请求

```c
uint8_t* request_data = NULL;
size_t request_size = 0;

if (MyService_getUser_SerializeRequest(&request, &request_data, &request_size) == 0) {
    // 使用 request_data 发送请求
}

// 记得释放内存
free(request_data);
```

### 反序列化请求（服务器端）

```c
MyService_getUser_Request_t request;
memset(&request, 0, sizeof(MyService_getUser_Request_t));

if (MyService_getUser_DeserializeRequest(request_data, request_size, &request) == 0) {
    // 使用请求数据
    printf("User ID: %ld\\n", (long)request.user_id);
}

// 记得释放内存
MyService_getUser_FreeRequest(&request);
```

### 序列化响应（服务器端）

```c
MyService_getUser_Response_t response;
memset(&response, 0, sizeof(MyService_getUser_Response_t));

response.id = 123;
response.name = strdup("John Doe");
response.email = strdup("john@example.com");

uint8_t* response_data = NULL;
size_t response_size = 0;

if (MyService_getUser_SerializeResponse(&response, &response_data, &response_size) == 0) {
    // 发送响应数据
}

// 释放内存
MyService_getUser_FreeResponse(&response);
free(response_data);
```

### 反序列化响应（客户端）

```c
MyService_getUser_Response_t response;
memset(&response, 0, sizeof(MyService_getUser_Response_t));

if (MyService_getUser_DeserializeResponse(response_data, response_size, &response) == 0) {
    printf("User: %s (%s)\\n", response.name, response.email);
}

// 释放内存
MyService_getUser_FreeResponse(&response);
```

## 内存管理

### 释放请求结构体

```c
MyService_getUser_Request_t request;
// ... 使用请求 ...

MyService_getUser_FreeRequest(&request);
```

### 释放响应结构体

```c
MyService_getUser_Response_t response;
// ... 使用响应 ...

MyService_getUser_FreeResponse(&response);
```

### 释放序列化数据

```c
uint8_t* data = NULL;
size_t size = 0;
// ... 序列化数据 ...

free(data);  // 释放序列化数据
```

## 在服务器中使用

```c
int my_get_user_handler(void* ctx,
                       const uint8_t* request_data,
                       size_t request_size,
                       uint8_t** response_data,
                       size_t* response_size) {
    (void)ctx;

    // 反序列化请求
    MyService_getUser_Request_t request;
    if (MyService_getUser_DeserializeRequest(request_data, request_size, &request) != 0) {
        return UVRPC_ERROR;
    }

    // 处理请求
    MyService_getUser_Response_t response;
    memset(&response, 0, sizeof(MyService_getUser_Response_t));

    // TODO: 实现业务逻辑
    response.id = request.user_id;
    response.name = strdup("User Name");
    response.email = strdup("user@example.com");

    // 序列化响应
    if (MyService_getUser_SerializeResponse(&response, response_data, response_size) != 0) {
        MyService_getUser_FreeRequest(&request);
        MyService_getUser_FreeResponse(&response);
        return UVRPC_ERROR;
    }

    // 清理
    MyService_getUser_FreeRequest(&request);
    MyService_getUser_FreeResponse(&response);

    return UVRPC_OK;
}

int main() {
    uv_loop_t* loop = uv_default_loop();
    uvrpc_server_t* server = uvrpc_server_new(loop, "tcp://*:5555", UVRPC_MODE_REQ_REP);

    // 注册服务
    uvrpc_server_register_service(server, "MyService.getUser", my_get_user_handler, NULL);

    uvrpc_server_start(server);
    uv_run(loop, UV_RUN_DEFAULT);

    uvrpc_server_free(server);
    uv_loop_close(loop);

    return 0;
}
```

## 在客户端中使用

```c
void get_user_callback(void* ctx, int status,
                       const uint8_t* response_data,
                       size_t response_size) {
    (void)ctx;

    if (status != UVRPC_OK) {
        fprintf(stderr, "RPC call failed: %s\\n", uvrpc_strerror(status));
        return;
    }

    // 反序列化响应
    MyService_getUser_Response_t response;
    if (MyService_getUser_DeserializeResponse(response_data, response_size, &response) == 0) {
        printf("User ID: %ld\\n", (long)response.id);
        printf("Name: %s\\n", response.name);
        printf("Email: %s\\n", response.email);
    }

    // 清理
    MyService_getUser_FreeResponse(&response);
}

int main() {
    uv_loop_t* loop = uv_default_loop();
    uvrpc_client_t* client = uvrpc_client_new(loop, "tcp://127.0.0.1:5555", UVRPC_MODE_REQ_REP);

    // 创建请求
    MyService_getUser_Request_t request;
    memset(&request, 0, sizeof(MyService_getUser_Request_t));
    request.user_id = 123;

    // 序列化请求
    uint8_t* request_data = NULL;
    size_t request_size = 0;
    MyService_getUser_SerializeRequest(&request, &request_data, &request_size);

    // 调用服务
    uvrpc_client_call(client, "MyService.getUser", "getUser",
                      request_data, request_size,
                      get_user_callback, NULL);

    // 清理
    free(request_data);
    MyService_getUser_FreeRequest(&request);

    uv_run(loop, UV_RUN_DEFAULT);

    uvrpc_client_free(client);
    uv_loop_close(loop);

    return 0;
}
```

## 类型说明

### 基本类型

| YAML 类型 | C 类型 | 示例 |
|-----------|--------|------|
| `string` | `char*` | `request.name = strdup("hello");` |
| `int` | `int64_t` | `request.count = 42;` |
| `float` | `double` | `request.price = 3.14;` |
| `bool` | `bool` | `request.active = true;` |
| `bytes` | `uint8_t*` | `request.data = malloc(size);` |

### 内存管理规则

1. **字符串类型**：使用 `strdup()` 分配，FreeRequest/FreeResponse 会自动释放
2. **二进制数据**：使用 `malloc()` 分配，FreeRequest/FreeResponse 会自动释放
3. **数组类型**：使用 `malloc()` 分配，FreeRequest/FreeResponse 会自动释放
4. **基本类型**：不需要手动释放

## 错误处理

所有序列化和反序列化函数返回 `0` 表示成功，非 `0` 表示失败。

```c
int rc = MyService_getUser_SerializeRequest(&request, &output, &size);
if (rc != 0) {
    fprintf(stderr, "Serialization failed: %d\\n", rc);
    return UVRPC_ERROR;
}
```

## 注意事项

1. **不要手动编辑生成的代码**：每次重新生成会覆盖修改
2. **内存泄漏**：确保调用 FreeRequest/FreeResponse 释放分配的内存
3. **空指针检查**：序列化前检查指针是否为 NULL
4. **字符串处理**：使用 `strdup()` 复制字符串，不要直接赋值
5. **数组大小**：对于数组类型，确保设置正确的 count/size 字段

## 示例项目

完整示例请参考：

- `examples/echo_service.yaml` - Echo 服务定义
- `generated/echoservice_server_example.c` - 服务器示例
- `generated/echoservice_client_example.c` - 客户端示例