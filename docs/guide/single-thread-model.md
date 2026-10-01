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

## 一次请求能带多大的数据

上限作用在**编码后的帧**上，不是载荷上；而且编码开销**不是常数** —— 实测 0 字节载荷
对应 38 字节帧、1 字节对应 51、100 字节对应 48（FlatBuffers 对齐所致）。所以
"最大载荷 = 65536 - 48" 只是近似。

实测三种 socket 传输的边界**各不相同**：

|传输|最大载荷|
|---|---|
|TCP|65488|
|IPC|65484|
|UDP|65452|

三者执行的是同一个 `UVBUS_DEFAULT_MAX_FRAME_SIZE`，落点不同只因为各自停止尺寸处的
编码开销不同；UDP 少得最多，符合它是数据报协议、有低于帧上限的自身天花板。

**所以跨传输统一的安全上限是三者中最低的 65452，不是 TCP 的 65488** —— 按 TCP 的边界
选载荷，换到 UDP 或 IPC 上会**静默收不到响应**。

超过上限是**整帧丢弃**，不是截断：服务端重置读缓冲，请求就没了，调用方只能等到自己的
请求超时，而不会收到一个短回答。

> `UVBUS_MAX_FRAME_SIZE`（1MB）在 `uvbus_config.h` 里定义了但全库无引用 —— 没有任何
> 载荷能接近它，实际生效的是 64KB 的默认值。它看起来像上限，其实不是。


## 响应迟迟不来：请求超时

三层拒绝都是**同步返回**的，但还有一种情况没法同步返回：请求已经发出去了，
响应却永远不来。这在会丢包的传输上是常态（UDP 丢一个响应，服务端毫不知情，
客户端也等不到任何东西）。

槽位原本只在响应到达时才释放，所以丢掉的响应会**一直占着它**，直到该客户端断开；
累积到配额上限后，客户端就永久返回 `UVRPC_ERROR_RATE_LIMITED`，
而这个错误和原因之间没有任何联系。现在每个待回调槽位带一个截止时间
（`timeout_ms`，默认 `UVRPC_DEFAULT_TIMEOUT_MS` = 30 秒）：

```c
uvrpc_config_t* cfg = uvrpc_config_set_timeout(uvrpc_config_new(), 2000);
uvrpc_client_t* client = uvrpc_client_create(cfg);
/* 也可以在运行时改 */
uvrpc_client_set_timeout(client, 0);   /* 0 = 关闭，恢复旧行为 */
```

到期后槽位归还，并以 `UVRPC_ERROR_TIMEOUT` 调用你注册的回调。

**契约要说准：通知不迟到于该客户端的"下一次调用"，而不是"到期那一刻"。**
实现里没有 per-client 定时器 —— 清扫发生在发出请求、配额检查之前，
所以一个发完请求就空闲的客户端不会被打断。这是有意的取舍：给每个客户端挂一个
libuv 定时器会让 `uvrpc_client_free()` 必须走异步句柄关闭（应用不泵 loop 就关 loop
会拿到 `UV_EBUSY`），而这个生命周期语义比"及时通知"更难改回来。
对持续发请求的应用（也就是会真正累积泄漏的那些），
下一次调用就在下一次清扫，感知不到差别。

另外注意：超时和丢包**不可区分**。没有响应，可能是包丢了、服务端慢了、
服务端崩了、或地址写错了——从客户端看这四件事完全一样。超时只告诉你"没等到"，
不告诉你"为什么"。

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

## 三处库会替你决定的事

单线程模型说的是**数据**没有共享；有三处影响**整个进程**的行为，它们不违背那个模型，
但调用方应该知道。

### 1. 使用 socket 传输时会忽略 SIGPIPE

服务端往客户端已关闭的连接写响应，内核抛 SIGPIPE，默认动作是**终止进程**。libuv 在
Linux 上只设 macOS 的 `SO_NOSIGPIPE`，所以信号照常送达；而 libuv 自己会把同一失败以
`UV_EPIPE` 交给写回调，那才是调用方能处理的地方。客户端超时、崩溃、掉网、负载均衡
清理空闲连接，都会走到这条路径 —— "客户端挂了就死的 TCP RPC 服务器"不是能上线的服务器。

因此创建 TCP/UDP/IPC 传输时会一次性 `signal(SIGPIPE, SIG_IGN)`。只用 INPROC 或 SAMELOOP
的应用不受影响。

**代价**：SIGPIPE 是进程级的，你的应用不能再依赖它处理自己的 socket。若你的程序需要
SIGPIPE 的默认行为，请在创建任何 socket 传输**之前**自行恢复，并接受随之而来的崩溃风险。

### 2. 请求失败用独立的帧类型

调错方法名、服务端返回错误时，客户端拿到的 `status` 非 0、`result_size` 为 0、
`frame_type` 为 `UVRPC_FRAME_TYPE_RESPONSE_ERROR`。

之所以要有独立的帧类型：错误帧和结果帧曾经是同一个帧，而 payload 无法自证 ——
handler 完全可以合法返回"4 字节整数 + 一段文本"。不区分时，调不存在的方法会返回
`status == UVRPC_OK`，而"结果"是错误消息的 ASCII 原文 —— **看起来完全不像错误**。

`frame_type` 现在是有名字的常量（`UVRPC_FRAME_TYPE_RESPONSE` 等），不再是魔数。
**这是线格式变更**：新服务端配旧客户端仍表现为"成功携带错误字节"；旧服务端配新客户端时
错误帧被忽略。0.x 阶段尚无发布，此刻改代价最低。

### 3. 释放还有回调在途的对象只是放下一份引用

`uvbus_free()` / `uvrpc_client_free()` 是**释放**不是释放内存：引用减一，归零才真正销毁。
连接还在途时传输自持一份引用，由连接回调归还。

所以释放之后**必须泵 loop**，回调才会真正执行完毕 —— 不泵，transport 和 client 会一直
挂着。这也是为什么每个测试在收尾时都排空 loop 并关闭自己的定时器句柄。


## 总结

UVRPC完全遵循单线程模型和loop注入模式：

✅ 单线程，零锁，零可变全局  
✅ Loop注入，用户控制事件循环  
✅ 异步I/O，非阻塞操作  
✅ 多实例支持，实例间无进程级共享状态  

> 例外见上节：使用 socket 传输时会忽略 SIGPIPE，这是进程级的。
✅ 高性能，可预测，易测试