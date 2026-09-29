# UVRPC 单线程模型和Loop注入模式保证

## 设计原则

UVRPC严格遵循以下设计原则：

1. **单线程模型**: 所有操作在单个libuv事件循环中执行
2. **零锁设计**: 不使用任何锁、互斥量或原子操作
3. **零可变全局**: 没有会被请求改写的文件作用域变量（`static const` 表除外）
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

- ✅ 无全局 loop 变量
- ✅ 无全局服务器/客户端实例
- ✅ 无 per-loop 隐藏状态：INPROC/SAMELOOP 的端点注册表由**调用方创建并传入**
  （`uvbus_loop_registry_new()` + `uvrpc_config_set_loop_registry()`），
  框架不在 loop 上留任何东西，详见 [架构文档](/architecture/)

### 唯一的全局变量

全项目只有一处文件作用域可变全局，且只在 custom 分配器构建里存在：

```c
/* src/uvrpc_allocator.c:48-50，被 #if UVRPC_DEFAULT_ALLOCATOR == CUSTOM 包住 */
static uvrpc_custom_allocator_t g_custom_allocator = {0};
```

它并非只读配置：`uvrpc_allocator_init()` 在运行时写入它（`:107`），传入 NULL 时
`memset` 清零（`:111`、`:123`）。安全前提是"注册分配器属于初始化动作"，运行中不切换；
框架自身不在请求路径上改写它。

默认分配器没有对应的运行时类型变量 —— 类型由编译期的 `UVRPC_DEFAULT_ALLOCATOR`
决定，`uvrpc_alloc/free` 通过预处理分支选择实现（`:10-15`）。system/mimalloc 构建
里分配器全局数量为 0。

其余文件作用域状态都是只读的：各传输的 `static const` vtable 落在
`.data.rel.ro.local`，重定位完成后不再改写。

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

## 资源满了怎么办：三层无锁拒绝

零线程零锁意味着"满了就挂起等待"在这个模型里无处安放 —— 一阻塞就卡死整个 loop。
所以三处上限统一是 **fail fast，把决定权交回调用者**，全都是同步返回值：

| 触发 | 返回码 | 判据 |
|---|---|---|
| 在途请求超配额 | `UVRPC_ERROR_RATE_LIMITED` | `max_concurrent > 0 && current_concurrent + 1 > max_concurrent`；单次调用与批量调用都检查 |
| 路由槽位被占 | `UVRPC_ERROR_CALLBACK_LIMIT` | `pending_callbacks[msgid & (N-1)]` 已被活跃条目占用（低位撞槽即算满，不等于真的到了 N） |
| 传输写队列满 | `UVRPC_ERROR_TRANSPORT_BUSY` | `uvbus_send()` 返回 `UVBUS_ERROR_BUFFER_FULL` |

两点要记住：`current_concurrent` 计的是**已登记回调、正等最终响应**的请求，oneway 不占名额；
`uvrpc_client_call_batch()` 是整批判定，超过配额时一帧都不发出。服务端另有一条同构拒绝：
连接数达 `max_clients` 时直接回错误响应帧。

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

- [x] 所有结构都有 loop 字段
- [x] 无全局 loop 变量
- [x] 无 pthread / mutex / atomic 调用（全量 grep 只命中 `uvbus_transport_udp.c:350` 的一条注释）
- [x] 无 mutex 调用
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

✅ 单线程，零锁，零可变全局  
✅ Loop注入，用户控制事件循环  
✅ 异步I/O，非阻塞操作  
✅ 多实例支持，实例间无进程级共享状态  
✅ 高性能，可预测，易测试