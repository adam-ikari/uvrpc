# UVRPC Layer 2 API 使用指南

## 概述

Layer 2 API 是 UVRPC 的中级API，提供了丰富的配置选项和性能调优能力。它在易用性和灵活性之间取得了平衡，适合需要特殊配置的场景，如 Inproc 通信、性能优化等。

## 核心特性

- ✅ 支持共享 ZMQ context（Inproc 通信）
- ✅ 丰富的性能配置选项
- ✅ 预设性能模式
- ✅ TCP/IPC 传输优化
- ✅ 统计信息获取

## API 分类

### 1. 创建 API（支持共享 context）

```c
/* 服务器 - 使用共享 context */
void* shared_ctx = zmq_ctx_new();
uvrpc_server_t* server = uvrpc_server_new_with_ctx(
    &loop,
    "inproc://my-service",      /* 地址 */
    UVRPC_MODE_ROUTER_DEALER,    /* ZMQ 模式 */
    shared_ctx                    /* 共享的 ZMQ context */
);

/* 客户端 - 使用共享 context */
uvrpc_client_t* client = uvrpc_client_new_with_ctx(
    &loop,
    "inproc://my-service",
    UVRPC_MODE_ROUTER_DEALER,
    shared_ctx
);

/* 清理 */
uvrpc_server_free(server);
uvrpc_client_free(client);
zmq_ctx_term(shared_ctx);  /* 最后释放 context */
```

### 2. 性能配置 API

#### 高水位标记（HWM）配置

```c
/* 设置 HWM - 控制队列深度 */
uvrpc_server_set_hwm(server, 10000, 10000);  /* 服务器 */
uvrpc_client_set_hwm(client, 10000, 10000);  /* 客户端 */

/* 获取 HWM */
int sndhwm, rcvhwm;
uvrpc_server_get_hwm(server, &sndhwm, &rcvhwm);
printf("SNDHWM: %d, RCVHWM: %d\n", sndhwm, rcvhwm);
```

**参数说明：**
- `sndhwm`: 发送队列高水位标记
- `rcvhwm`: 接收队列高水位标记
- 较小的 HWM（如 100）：减少延迟，适合实时性要求高的场景
- 较大的 HWM（如 10000）：提高吞吐量，适合批量处理场景

#### TCP 缓冲区配置

```c
/* 设置 TCP 缓冲区大小 */
uvrpc_server_set_tcp_buffer_size(server, 256 * 1024, 256 * 1024);
uvrpc_client_set_tcp_buffer_size(client, 256 * 1024, 256 * 1024);
```

**参数说明：**
- `sndbuf`: 发送缓冲区大小（字节）
- `rcvbuf`: 接收缓冲区大小（字节）
- 默认值：256KB
- 增大缓冲区可提高大数据传输性能

#### TCP Keepalive 配置

```c
/* 启用 TCP keepalive */
uvrpc_client_set_tcp_keepalive(
    client,
    1,      /* 启用 */
    60,     /* 空闲时间（秒） */
    5,      /* 探测次数 */
    10      /* 探测间隔（秒） */
);
```

**参数说明：**
- 用于检测死连接，自动断开无响应的连接
- 仅对 TCP 传输有效

#### 重连间隔配置

```c
/* 设置重连间隔 */
uvrpc_client_set_reconnect_interval(
    client,
    100,    /* 初始重连间隔（毫秒） */
    5000    /* 最大重连间隔（毫秒） */
);
```

**参数说明：**
- ZMQ 会自动在 initial_ms 和 max_ms 之间指数退避重连
- 默认：初始 100ms，最大不限制

#### I/O 线程数配置

```c
/* 设置 I/O 线程数 */
uvrpc_server_set_io_threads(server, 2);  /* 服务器推荐 2-4 */
uvrpc_client_set_io_threads(client, 1);  /* 客户端通常 1 足够 */
```

**参数说明：**
- 必须在连接/启动之前设置
- 增加线程数可提高高并发场景性能
- 会增加资源消耗

#### Linger 选项

```c
/* 设置 Linger 选项 */
uvrpc_client_set_linger(client, 0);  /* 立即关闭（推荐） */
```

**参数说明：**
- -1：无限等待（不推荐）
- 0：立即丢弃未发送的消息（默认，推荐）
- >0：等待指定毫秒后丢弃未发送的消息

### 3. 统计信息 API

```c
/* 获取服务器统计 */
int services_count;
uvrpc_server_get_stats(server, &services_count);
printf("Services: %d\n", services_count);

/* 获取客户端统计 */
int pending_requests;
uvrpc_client_get_stats(client, &pending_requests);
printf("Pending Requests: %d\n", pending_requests);
```

### 4. 性能预设模式

```c
typedef enum {
    UVRPC_PERF_LOW_LATENCY = 0,    /* 低延迟模式 */
    UVRPC_PERF_BALANCED = 1,        /* 平衡模式（默认） */
    UVRPC_PERF_HIGH_THROUGHPUT = 2  /* 高吞吐模式 */
} uvrpc_performance_mode_t;

/* 应用性能预设 */
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_LOW_LATENCY);
uvrpc_client_apply_performance_mode(client, UVRPC_PERF_LOW_LATENCY);
```

**预设配置详情：**

| 模式 | HWM | Batch Size | TCP Buffer | IO Threads | 适用场景 |
|-----|-----|-----------|-----------|-----------|---------|
| **LOW_LATENCY** | 100 | 1 | 64KB | 1 | 实时性要求高的场景 |
| **BALANCED** | 1000 | 10 | 256KB | 2 | 一般场景（默认） |
| **HIGH_THROUGHPUT** | 10000 | 100 | 1MB | 4 | 大批量处理场景 |

## 使用示例

### 示例 1: Inproc 通信（单进程内）

```c
void* shared_ctx = zmq_ctx_new();

uv_loop_t loop;
uv_loop_init(&loop);

/* 服务器 */
uvrpc_server_t* server = uvrpc_server_new_with_ctx(
    &loop,
    "inproc://my-service",
    UVRPC_MODE_ROUTER_DEALER,
    shared_ctx
);
uvrpc_server_register_service(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);

/* 客户端 */
uvrpc_client_t* client = uvrpc_client_new_with_ctx(
    &loop,
    "inproc://my-service",
    UVRPC_MODE_ROUTER_DEALER,
    shared_ctx
);
uvrpc_client_connect(client);

/* 使用并发 await 获得最佳性能 */
uvrpc_async_t* async = uvrpc_async_new(&loop);
uvrpc_client_call_async(client, "echo", "echo", data, size, async);
uvrpc_await(async);

/* 清理 */
uvrpc_async_free(async);
uvrpc_client_free(client);
uvrpc_server_free(server);
uv_loop_close(&loop);
zmq_ctx_term(shared_ctx);
```

### 示例 2: 低延迟配置

```c
uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://0.0.0.0:5555", UVRPC_MODE_ROUTER_DEALER);

/* 应用低延迟预设 */
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_LOW_LATENCY);

/* 或者手动配置 */
uvrpc_server_set_hwm(server, 100, 100);
uvrpc_server_set_tcp_buffer_size(server, 64 * 1024, 64 * 1024);
uvrpc_server_set_io_threads(server, 1);

uvrpc_server_register_service(server, "service", handler, NULL);
uvrpc_server_start(server);
```

### 示例 3: 高吞吐配置

```c
uvrpc_client_t* client = uvrpc_client_new(&loop, "tcp://server:5555", UVRPC_MODE_ROUTER_DEALER);

/* 应用高吞吐预设 */
uvrpc_client_apply_performance_mode(client, UVRPC_PERF_HIGH_THROUGHPUT);

/* 启用 TCP keepalive */
uvrpc_client_set_tcp_keepalive(client, 1, 60, 5, 10);

/* 设置重连间隔 */
uvrpc_client_set_reconnect_interval(client, 100, 5000);

uvrpc_client_connect(client);
```

### 示例 4: 自定义配置

```c
uvrpc_server_t* server = uvrpc_server_new(&loop, "tcp://0.0.0.0:5555", UVRPC_MODE_ROUTER_DEALER);

/* 完全自定义配置 */
uvrpc_server_set_hwm(server, 5000, 5000);
uvrpc_server_set_tcp_buffer_size(server, 512 * 1024, 512 * 1024);
uvrpc_server_set_io_threads(server, 3);

uvrpc_server_register_service(server, "service", handler, NULL);
uvrpc_server_start(server);

/* 获取统计信息 */
int services_count;
uvrpc_server_get_stats(server, &services_count);
printf("Registered services: %d\n", services_count);
```

## 性能优化建议

### 低延迟场景

```c
/* 服务器 */
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_LOW_LATENCY);

/* 客户端 */
uvrpc_client_apply_performance_mode(client, UVRPC_PERF_LOW_LATENCY);
uvrpc_client_set_batch_size(client, 1);  /* 禁用批量 */
```

### 高吞吐场景

```c
/* 服务器 */
uvrpc_server_apply_performance_mode(server, UVRPC_PERF_HIGH_THROUGHPUT);

/* 客户端 */
uvrpc_client_apply_performance_mode(client, UVRPC_PERF_HIGH_THROUGHPUT);
uvrpc_client_set_batch_size(client, 100);  /* 启用批量 */
```

### 网络不稳定环境

```c
/* 客户端配置 */
uvrpc_client_set_tcp_keepalive(client, 1, 60, 5, 10);
uvrpc_client_set_reconnect_interval(client, 100, 30000);  /* 更长的重连间隔 */
uvrpc_client_set_linger(client, 0);  /* 快速关闭 */
```

## 常见问题

### Q: 什么时候需要使用共享 context？

A: 当使用 Inproc 传输时，server 和 client 必须共享同一个 ZMQ context。

### Q: HWM 设置多少合适？

A: 
- 低延迟场景：100-500
- 一般场景：1000-5000
- 高吞吐场景：10000-50000

### Q: 如何选择性能模式？

A:
- 实时性要求高：LOW_LATENCY
- 不确定或平衡需求：BALANCED
- 批量处理需求：HIGH_THROUGHPUT

### Q: I/O 线程数设置多少？

A:
- 客户端：通常 1 足够
- 服务器：2-4 个线程，根据负载调整

## 参考

- [API 设计哲学](API_DESIGN_PHILOSOPHY.md)
- [性能指南](PERFORMANCE.md)
- [事件循环模式](EVENT_LOOP_MODES.md)
