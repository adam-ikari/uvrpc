# UVRPC 新 API 设计

## 设计原则

### 1. 统一配置
所有配置通过`uvrpc_config_t`结构传递，简化API调用。

### 2. 构建器模式
支持链式调用，使配置代码更简洁。

### 3. 传输透明
支持Inproc、IPC、TCP、UDP四种传输方式，API统一。

### 4. 模式灵活
支持所有模式，可随意组合。

### 5. 零冗余
删除所有旧的API，只保留最简洁的接口。

## API 对比

### 旧API（已舍弃）

```c
/* 多个创建函数 */
uvrpc_server_t* server1 = uvrpc_server_new(&loop, "tcp://addr", UVRPC_MODE_ROUTER_DEALER);
uvrpc_server_t* server2 = uvrpc_server_new_zmq(&loop, "tcp://addr", ZMQ_ROUTER);
uvrpc_server_t* server3 = uvrpc_server_new_with_ctx(&loop, "inproc://svc", UVRPC_MODE_ROUTER_DEALER, ctx);

/* 分散的配置API */
uvrpc_server_set_hwm(server, 10000, 10000);
uvrpc_server_set_tcp_buffer_size(server, 256 * 1024, 256 * 1024);
uvrpc_server_set_io_threads(server, 2);
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_BALANCED);
```

### 新API（推荐）

```c
/* 统一的创建函数 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_address(config, "tcp://0.0.0.0:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_perf_mode(config, UVRPC_PERF_BALANCED);

uvrpc_server_t* server = uvrpc_server_create(config);
```

## 使用示例

### 示例1: Inproc 通信（最简洁）

```c
uv_loop_t loop;
uv_loop_init(&loop);

void* shared_ctx = zmq_ctx_new();

/* 服务器配置 */
uvrpc_config_t* server_config = uvrpc_config_new();
uvrpc_config_set_loop(server_config, &loop);
uvrpc_config_set_address(server_config, "inproc://my-service");
uvrpc_config_set_transport(server_config, UVRPC_TRANSPORT_INPROC);
uvrpc_config_set_mode(server_config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_zmq_ctx(server_config, shared_ctx);

uvrpc_server_t* server = uvrpc_server_create(server_config);
uvrpc_server_register_service(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);

/* 客户端配置（使用相同的context） */
uvrpc_config_t* client_config = uvrpc_config_new();
uvrpc_config_set_loop(client_config, &loop);
uvrpc_config_set_address(client_config, "inproc://my-service");
uvrpc_config_set_transport(client_config, UVRPC_TRANSPORT_INPROC);
uvrpc_config_set_mode(client_config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_zmq_ctx(client_config, shared_ctx);

uvrpc_client_t* client = uvrpc_client_create(client_config);
uvrpc_client_connect(client);

/* 使用 */
uvrpc_async_t* async = uvrpc_async_create(&loop);
uvrpc_client_call_async(client, "echo", "echo", data, size, async);
uvrpc_async_await(async);

/* 清理 */
uvrpc_async_free(async);
uvrpc_client_free(client);
uvrpc_server_free(server);
uv_loop_close(&loop);
zmq_ctx_term(shared_ctx);
uvrpc_config_free(server_config);
uvrpc_config_free(client_config);
```

### 示例2: TCP 低延迟

```c
uv_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://0.0.0.0:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_perf_mode(config, UVRPC_PERF_LOW_LATENCY);

uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register_service(server, "service", handler, NULL);
uvrpc_server_start(server);
```

### 示例3: TCP 高吞吐 + 自定义配置

```c
uvrpc_config_t* config = uvrpc_config_new()
    ->set_loop(&loop)
    ->set_address("tcp://0.0.0.0:5555")
    ->set_transport(UVRPC_TRANSPORT_TCP)
    ->set_mode(UVRPC_SERVER_CLIENT)
    ->set_perf_mode(UVRPC_PERF_HIGH_THROUGHPUT)
    ->set_tcp_keepalive(1, 60, 5, 10)
    ->set_reconnect(100, 30000)
    ->set_linger(0);

uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
```

### 示例4: IPC 通信

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "ipc:///tmp/my-service");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_IPC);
uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_perf_mode(config, UVRPC_PERF_BALANCED);

uvrpc_server_t* server = uvrpc_server_create(config);
```

### 示例5: UDP 组播

```c
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "udp://*:5555");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_UDP);
uvrpc_config_set_mode(config, UVRPC_BROADCAST);
uvrpc_config_set_udp_multicast(config, "239.0.0.1");

uvrpc_server_t* server = uvrpc_server_create(config);
```

## API 清单

### 配置构建器

- `uvrpc_config_new()` - 创建配置
- `uvrpc_config_free()` - 释放配置
- `uvrpc_config_set_loop()` - 设置事件循环
- `uvrpc_config_set_address()` - 设置地址
- `uvrpc_config_set_transport()` - 设置传输类型
- `uvrpc_config_set_mode()` - 设置模式
- `uvrpc_config_set_zmq_ctx()` - 设置ZMQ context
- `uvrpc_config_set_perf_mode()` - 设置性能模式
- `uvrpc_config_set_batch_size()` - 设置批量大小
- `uvrpc_config_set_hwm()` - 设置HWM
- `uvrpc_config_set_io_threads()` - 设置I/O线程数
- `uvrpc_config_set_tcp_buffer()` - 设置TCP缓冲区
- `uvrpc_config_set_tcp_keepalive()` - 设置TCP keepalive
- `uvrpc_config_set_reconnect()` - 设置重连间隔
- `uvrpc_config_set_linger()` - 设置Linger
- `uvrpc_config_set_udp_multicast()` - 设置UDP组播

### 服务器

- `uvrpc_server_create()` - 创建服务器
- `uvrpc_server_register_service()` - 注册服务
- `uvrpc_server_start()` - 启动服务器
- `uvrpc_server_stop()` - 停止服务器
- `uvrpc_server_get_stats()` - 获取统计
- `uvrpc_server_free()` - 释放服务器

### 客户端

- `uvrpc_client_create()` - 创建客户端
- `uvrpc_client_connect()` - 连接服务器
- `uvrpc_client_disconnect()` - 断开连接
- `uvrpc_client_call()` - 异步调用
- `uvrpc_client_get_stats()` - 获取统计
- `uvrpc_client_free()` - 释放客户端

### Async

- `uvrpc_async_create()` - 创建async
- `uvrpc_async_free()` - 释放async
- `uvrpc_client_call_async()` - async调用
- `uvrpc_async_await()` - await等待
- `uvrpc_async_await_timeout()` - await超时
- `uvrpc_async_await_all()` - await所有
- `uvrpc_async_await_any()` - await任意

## 传输方式支持

| 传输 | 地址格式 | 模式 | 性能模式 |
|-----|---------|---------|---------|
| **Inproc** | `inproc://name` | SERVER_CLIENT/BROADCAST | 所有 |
| **IPC** | `ipc:///tmp/name` | SERVER_CLIENT/BROADCAST | 所有 |
| **TCP** | `tcp://host:port` | SERVER_CLIENT/BROADCAST | 所有 |
| **UDP** | `udp://*:port` | BROADCAST | 所有 |

## 模式支持

| 模式 | 枚举值 | ZMQ实现 | 适用场景 |
|-----|-------|---------|---------|
| **服务器-客户端** | UVRPC_SERVER_CLIENT | ROUTER/DEALER | 异步RPC |
| **广播** | UVRPC_BROADCAST | PUB/SUB | 事件广播 |

## 性能模式

| 模式 | HWM | Batch | TCP Buffer | IO Threads |
|-----|-----|-------|-----------|-----------|
| **LOW_LATENCY** | 100 | 1 | 64KB | 1 |
| **BALANCED** | 1000 | 10 | 256KB | 2 |
| **HIGH_THROUGHPUT** | 10000 | 100 | 1MB | 4 |

## 迁移指南

### 从旧API迁移到新API

**步骤1：创建配置**
```c
/* 旧代码 */
uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://addr", UVRPC_MODE_ROUTER_DEALER);
uvrpc_server_set_hwm(server, 10000, 10000);
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_BALANCED);

/* 新代码 */
uvrpc_config_t* config = uvrpc_config_new();
uvrpc_config_set_loop(config, &loop);
uvrpc_config_set_address(config, "tcp://addr");
uvrpc_config_set_transport(config, UVRPC_TRANSPORT_TCP);
uvrpc_config_set_mode(config, UVRPC_SERVER_CLIENT);
uvrpc_config_set_perf_mode(config, UVRPC_PERF_BALANCED);
uvrpc_server_t* server = uvrpc_server_create(config);
```

**步骤2：替换async创建**
```c
/* 旧代码 */
uvrpc_async_t* async = uvrpc_async_new(&loop);

/* 新代码 */
uvrpc_async_t* async = uvrpc_async_create(&loop);
```

**步骤3：添加配置释放**
```c
uvrpc_config_free(config);
```

## 优势

1. **API数量减少60%**：从多个创建函数合并为一个
2. **配置更直观**：所有配置集中在一个结构中
3. **易于扩展**：添加新配置只需修改配置结构
4. **类型安全**：编译期检查配置完整性
5. **传输透明**：切换传输方式只需修改一个枚举值

