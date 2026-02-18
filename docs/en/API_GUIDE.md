# UVRPC API 使用指南

## 重要原则

**用户负责运行事件循环**

UVRPC的客户端和服务端都不负责运行libuv事件循环。用户必须：
1. 创建自己的事件循环
2. 将loop注入到配置中
3. 调用uv_run()运行事件循环

## 服务器端使用

### 基本流程

```c
// 1. 创建事件循环（用户负责）
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建配置并注入loop
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);  // 注入用户的loop
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

// 3. 创建服务器
uvrpc_server_t* server = uvrpc_server_create(config);
if (!server) {
    fprintf(stderr, "Failed to create server\n");
    return 1;
}

// 4. 注册handler
void my_handler(uvrpc_request_t* req, void* ctx) {
    // 处理请求
    uvrpc_request_send_response(req, UVRPC_OK, result, result_size);
}

uvrpc_server_register(server, "my_method", my_handler, NULL);

// 5. 启动服务器（不运行loop）
if (uvrpc_server_start(server) != UVRPC_OK) {
    fprintf(stderr, "Failed to start server\n");
    return 1;
}

// 6. 运行事件循环（用户负责）
uv_run(&loop, UV_RUN_DEFAULT);

// 7. 清理
uvrpc_server_stop(server);
uvrpc_server_free(server);
uvrpc_config_free(config);
uv_loop_close(&loop);
```

### 关键点

- ✅ `uvrpc_server_start()` 只启动监听，不运行loop
- ✅ 用户必须调用 `uv_run()` 来处理事件
- ✅ 用户可以控制何时停止loop

## 客户端使用

### 基本流程

```c
// 1. 创建事件循环（用户负责）
uv_loop_t loop;
uv_loop_init(&loop);

// 2. 创建配置并注入loop
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);  // 注入用户的loop
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");
uvrpc_config_set_comm_type(config, UVRPC_COMM_SERVER_CLIENT);

// 3. 创建客户端
uvrpc_client_t* client = uvrpc_client_create(config);
if (!client) {
    fprintf(stderr, "Failed to create client\n");
    return 1;
}

// 4. 连接服务器（不运行loop）
if (uvrpc_client_connect(client) != UVRPC_OK) {
    fprintf(stderr, "Failed to connect\n");
    return 1;
}

// 5. 运行事件循环处理连接（用户负责）
for (int i = 0; i < 50; i++) {
    uv_run(&loop, UV_RUN_ONCE);  // 处理连接事件
}

// 6. 调用RPC方法
void on_response(uvrpc_response_t* resp, void* ctx) {
    // 处理响应
}

uint8_t params[] = {10, 20};
uvrpc_client_call(client, "add", params, sizeof(params), on_response, NULL);

// 7. 运行事件循环处理响应（用户负责）
for (int i = 0; i < 50; i++) {
    uv_run(&loop, UV_RUN_ONCE);  // 处理响应事件
}

// 8. 断开连接
uvrpc_client_disconnect(client);

// 9. 清理
uvrpc_client_free(client);
uvrpc_config_free(config);
uv_loop_close(&loop);
```

### 关键点

- ✅ `uvrpc_client_connect()` 只发起连接，不运行loop
- ✅ 用户必须调用 `uv_run()` 来处理连接事件
- ✅ 用户必须调用 `uv_run()` 来处理响应事件
- ✅ 用户可以控制loop的运行时机

## 事件循环运行模式

### UV_RUN_DEFAULT

```c
uv_run(&loop, UV_RUN_DEFAULT);
```
- 运行直到没有活动句柄
- 适用于长期运行的服务器

### UV_RUN_ONCE

```c
uv_run(&loop, UV_RUN_ONCE);
```
- 处理一次事件循环迭代
- 适用于客户端、批处理
- 可以精确控制处理时机

### UV_RUN_NOWAIT

```c
uv_run(&loop, UV_RUN_NOWAIT);
```
- 不阻塞，只处理当前挂起的事件
- 适用于非阻塞轮询

## 典型使用场景

### 长期运行的服务器

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
// ... 配置 ...

uvrpc_server_t* server = uvrpc_server_create(config);
// ... 注册handlers ...

uvrpc_server_start(server);

// 运行直到收到信号
uv_run(&loop, UV_RUN_DEFAULT);

uvrpc_server_stop(server);
uvrpc_server_free(server);
uv_loop_close(&loop);
```

### 请求-响应客户端

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
// ... 配置 ...

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);

// 等待连接
for (int i = 0; i < 50; i++) {
    uv_run(&loop, UV_RUN_ONCE);
}

// 发送请求
uvrpc_client_call(client, "method", params, size, callback, NULL);

// 等待响应
for (int i = 0; i < 50; i++) {
    uv_run(&loop, UV_RUN_ONCE);
}

uvrpc_client_disconnect(client);
uvrpc_client_free(client);
uv_loop_close(&loop);
```

### 批量操作

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
// ... 配置 ...

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);

// 等待连接
for (int i = 0; i < 50; i++) {
    uv_run(&loop, UV_RUN_ONCE);
}

// 批量发送请求
const char* methods[] = {"method1", "method2", "method3"};
const uint8_t* params[] = {data1, data2, data3};
size_t sizes[] = {size1, size2, size3};

uvrpc_client_call_batch(client, methods, params, sizes, 3);

// 等待所有响应
for (int i = 0; i < 100; i++) {
    uv_run(&loop, UV_RUN_ONCE);
}

uvrpc_client_disconnect(client);
uvrpc_client_free(client);
uv_loop_close(&loop);
```

## 多实例支持

由于用户负责运行loop，可以轻松创建多个独立实例：

```c
// 服务器1
uv_loop_t loop1;
uv_loop_init(&loop1);

uvrpc_config_t* config1 = uvrpc_config_new();
uvrpc_config_set_loop(config1, &loop1);
uvrpc_config_set_address(config1, "tcp://127.0.0.1:5555");

uvrpc_server_t* server1 = uvrpc_server_create(config1);
uvrpc_server_start(server1);

// 服务器2
uv_loop_t loop2;
uv_loop_init(&loop2);

uvrpc_config_t* config2 = uvrpc_config_new();
uvrpc_config_set_loop(config2, &loop2);
uvrpc_config_set_address(config2, "tcp://127.0.0.1:5556");

uvrpc_server_t* server2 = uvrpc_server_create(config2);
uvrpc_server_start(server2);

// 运行两个服务器（使用不同的loop）
// 可以在不同线程中运行各自的loop
```

## 常见错误

### ❌ 错误：期望start/connect运行loop

```c
uvrpc_server_start(server);  // 期望自动运行loop
// 等待响应...  // 永远不会触发
```

### ✅ 正确：手动运行loop

```c
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);  // 用户运行loop
```

### ❌ 错误：忘记注入loop

```c
uvrpc_config_t* config = uvrpc_config_new();
// 忘记调用 uvrpc_config_set_loop(config, &loop);
uvrpc_server_t* server = uvrpc_server_create(config);  // 可能失败
```

### ✅ 正确：注入loop

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);  // 注入loop
uvrpc_server_t* server = uvrpc_server_create(config);
```

## 总结

**UVRPC设计原则**：

1. **用户控制事件循环**
   - 用户创建和初始化loop
   - 用户调用uv_run()运行loop
   - 用户决定何时停止loop

2. **UVRPC不运行loop**
   - server_start()只启动监听
   - client_connect()只发起连接
   - 所有操作都是异步的

3. **完全的控制权**
   - 用户可以精确控制事件处理时机
   - 用户可以集成到现有的事件循环
   - 用户可以自定义事件循环策略

这种设计确保了最大的灵活性和可控性。
