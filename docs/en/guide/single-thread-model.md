# UVRPC 单线程模型和Loop注入模式保证

## 设计原则

UVRPC严格遵循以下设计原则：

1. **单线程模型**: 所有操作在单个libuv事件循环中执行
2. **零锁设计**: 不使用任何锁、互斥量或原子操作
3. **零全局变量**: 不使用全局变量（只读配置除外）
4. **Loop注入模式**: 通过配置注入用户的事件循环

## Loop注入模式

### 配置注入

```c
uv_loop_t loop;
uv_loop_init(&loop);

uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);  // 注入用户的事件循环
uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

uvrpc_server_t* server = uvrpc_server_create(config);
```

### 结构体设计

所有核心结构都包含loop指针：

```c
struct uvrpc_config {
    uv_loop_t* loop;  // 用户注入的事件循环
    // ...
};

struct uvrpc_server {
    uv_loop_t* loop;  // 从config继承
    // ...
};

struct uvrpc_client {
    uv_loop_t* loop;  // 从config继承
    // ...
};

struct uvrpc_transport {
    uv_loop_t* loop;  // 从server/client继承
    // ...
};
```

### 事件循环使用

```c
// 服务器
uvrpc_server_start(server);
uv_run(&loop, UV_RUN_DEFAULT);  // 用户控制事件循环

// 客户端
uvrpc_client_connect(client);
uv_run(&loop, UV_RUN_ONCE);  // 处理连接事件
uv_run(&loop, UV_RUN_ONCE);  // 处理响应
```

## 单线程保证

### 无线程代码

- ✅ 无pthread_create
- ✅ 无std::thread
- ✅ 无线程池
- ✅ 无工作队列

### 无锁设计

- ✅ 无pthread_mutex
- ✅ 无std::mutex
- ✅ 无spinlock
- ✅ 无原子操作

### 无全局状态

- ✅ 无全局loop变量
- ✅ 无全局服务器实例
- ✅ 无全局客户端实例
- ✅ 无共享数据结构

### 允许的全局变量

只读配置变量（初始化后不修改）：

```c
static uvrpc_allocator_type_t g_allocator_type = UVRPC_DEFAULT_ALLOCATOR;
static uvrpc_custom_allocator_t g_custom_allocator = {0};
```

这些变量在单线程模型下是安全的，因为：
1. 只在初始化时设置
2. 运行时只读
3. 无并发访问

## 并发模型

### 同步操作

所有I/O操作都是异步的，使用libuv回调：

```c
// 连接
uvrpc_client_connect(client);  // 非阻塞，返回后立即
// 连接完成时触发回调

// 请求
uvrpc_client_call(client, "method", data, size, callback, ctx);  // 非阻塞
// 响应到达时触发callback
```

### 事件循环

所有事件在同一个事件循环中串行处理：

```c
uv_run(&loop, UV_RUN_DEFAULT);
```

libuv保证：
- 同一时间只有一个回调执行
- 无并发访问共享数据
- 无需锁保护

## 多实例支持

由于使用loop注入模式，可以创建多个独立实例：

```c
uv_loop_t loop1, loop2;
uv_loop_init(&loop1);
uv_loop_init(&loop2);

uvrpc_config_t* config1 = uvrpc_config_new();
uvrpc_config_set_loop(config1, &loop1);

uvrpc_config_t* config2 = uvrpc_config_new();
uvrpc_config_set_loop(config2, &loop2);

uvrpc_server_t* server1 = uvrpc_server_create(config1);
uvrpc_server_t* server2 = uvrpc_server_create(config2);

// 两个服务器独立运行，无共享状态
```

## 生成的代码

RPC DSL生成的代码也遵循单线程模式：

```c
// 生成的server_stub.c
void rpc_register_all(uvrpc_server_t* server) {
    // 只注册，不创建线程
    uvrpc_server_register(server, "Add", rpc_handler, NULL);
}

// 用户实现 (rpc_user_impl.c)
int rpc_handle_request(const char* method_name, const void* request, uvrpc_request_t* req) {
    // 同步处理，无并发
    // 使用req->loop发送响应
}
```

## 优势

1. **高性能**: 无锁竞争，无上下文切换
2. **可预测**: 串行执行，易于调试
3. **可扩展**: 支持多实例，多进程
4. **云原生**: 适合容器化部署
5. **单元测试友好**: 易于mock和测试

## 验证

### 检查清单

- [x] 所有结构都有loop字段
- [x] 无全局loop变量
- [x] 无pthread调用
- [x] 无mutex调用
- [x] 无原子操作
- [x] 所有I/O使用libuv回调
- [x] 生成代码遵循相同模式

### 测试

```bash
# 多实例测试
./dist/bin/rpc_dsl_usage_example server tcp://127.0.0.1:5556 &
./dist/bin/rpc_dsl_usage_example server tcp://127.0.0.1:5557 &
# 两个服务器独立运行，无冲突
```

## 总结

UVRPC完全遵循单线程模型和loop注入模式：

✅ 单线程，零锁，零全局变量  
✅ Loop注入，用户控制事件循环  
✅ 异步I/O，非阻塞操作  
✅ 多实例支持，无共享状态  
✅ 高性能，可预测，易测试