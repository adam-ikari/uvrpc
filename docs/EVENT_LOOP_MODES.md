# libuv 事件循环模式指南

## 概述

uvrpc 基于 libuv 事件循环框架，支持单线程异步 I/O 模型。正确选择事件循环模式对性能和可靠性至关重要。

## libuv 事件循环的三种模式

### 1. UV_RUN_DEFAULT（生产环境推荐）

```c
uv_run(&loop, UV_RUN_DEFAULT);
```

**工作方式**：
- 持续运行事件循环，直到没有活动句柄或调用 `uv_stop()`
- 有事件时：立即处理
- 无事件时：阻塞等待，不占用 CPU

**适用场景**：
- ✅ 长期运行的服务器应用
- ✅ 需要自动管理生命周期的应用
- ✅ 生产环境的服务端和客户端

**示例**：
```c
/* 服务器模式 - 生产环境 */
uv_run(&loop, UV_RUN_DEFAULT);

/* 客户端模式 - 长期运行 */
uv_run(&loop, UV_RUN_DEFAULT);
```

**优势**：
- 简单直接，无需手动控制循环
- 自动管理生命周期
- 无事件时自动阻塞，不占用 CPU
- 类似 Node.js、txiki.js 的实现方式

### 2. UV_RUN_ONCE（性能测试推荐）

```c
while (condition) {
    uv_run(&loop, UV_RUN_ONCE);
}
```

**工作方式**：
- 轮询 I/O 一次，处理所有待处理的事件后返回
- 如果没有待处理的回调会阻塞等待
- 返回非零表示还有待处理的事件

**适用场景**：
- ✅ 性能测试和基准测试
- ✅ 需要精确控制退出条件的场景
- ✅ 需要准确测量性能指标的场景

**示例**：
```c
/* 性能测试 - 精确控制退出 */
while (completed < NUM_REQUESTS) {
    if (time(NULL) - timeout_start > TIMEOUT_SECONDS) {
        break;
    }
    uv_run(&loop, UV_RUN_ONCE);
}
```

**优势**：
- 可以精确控制退出条件
- 提供更好的性能控制
- 代码简单直接，易于理解
- 避免了 uv_idle 或 uv_stop 等机制带来的性能开销

### 3. UV_RUN_NOWAIT

```c
while (condition) {
    uv_run(&loop, UV_RUN_NOWAIT);
}
```

**工作方式**：
- 轮询 I/O 一次，立即返回（不阻塞）
- 总是立即返回，从不阻塞

**适用场景**：
- ✅ 嵌入到其他事件循环中
- ✅ 需要非阻塞轮询的场景

## 性能测试程序的选择

### 为什么使用 UV_RUN_ONCE？

测试程序 `test_performance.c` 使用 `UV_RUN_ONCE` 模式，原因如下：

1. **精确控制退出条件**
   - 可以准确测量完成所有请求所需的时间
   - 可以在超时时立即退出
   - 避免不必要的等待

2. **性能指标准确**
   - 可以精确控制请求发送和响应接收的时间窗口
   - 减少外部因素对性能测量的影响

3. **代码简单直接**
   - 避免了 uv_idle 或 uv_stop 等机制带来的复杂性
   - 更容易理解和维护
   - 减少潜在的错误

4. **性能开销最小**
   - 不需要额外的回调或定时器
   - 减少了函数调用开销
   - 性能可达 80,000+ ops/s

## 生产环境的推荐方案

### 服务器应用

```c
uv_loop_t loop;
uv_loop_init(&loop);

/* 创建服务器 */
uvrpc_server_t* server = uvrpc_server_new(&loop, bind_addr, UVRPC_MODE_ROUTER_DEALER);
uvrpc_server_register_service(server, "echo", handler, NULL);
uvrpc_server_start(server);

/* 使用 UV_RUN_DEFAULT - 持续运行 */
uv_run(&loop, UV_RUN_DEFAULT);

/* 清理 */
uvrpc_server_free(server);
uv_loop_close(&loop);
```

### 客户端应用（长期运行）

```c
uv_loop_t loop;
uv_loop_init(&loop);

/* 创建客户端 */
uvrpc_client_t* client = uvrpc_client_new(&loop, server_addr, UVRPC_MODE_ROUTER_DEALER);
uvrpc_client_connect(client);

/* 使用 UV_RUN_DEFAULT - 持续运行 */
uv_run(&loop, UV_RUN_DEFAULT);

/* 清理 */
uvrpc_client_free(client);
uv_loop_close(&loop);
```

## 性能对比

| 模式 | 吞吐量 | 适用场景 |
|------|--------|----------|
| UV_RUN_DEFAULT | N/A（持续运行） | 生产环境 |
| UV_RUN_ONCE | 80,000+ ops/s | 性能测试 |
| UV_RUN_NOWAIT | 较低 | 嵌入场景 |

## 最佳实践

1. **生产环境**：使用 `UV_RUN_DEFAULT`
   - 简单直接
   - 自动管理生命周期
   - 无 CPU 空转

2. **性能测试**：使用 `UV_RUN_ONCE`
   - 精确控制退出
   - 准确测量性能
   - 代码简单

3. **避免在回调中调用 uv_stop()**
   - 可能在事件循环的中间状态强制停止
   - 违背 libuv 的最佳实践

4. **避免在 uv_idle 中发送大量请求**
   - uv_idle 会在每次事件循环迭代时被调用
   - 会严重影响性能

## 参考资料

- [libuv 官方文档 - Event Loop](https://docs.libuv.org/en/latest/loop.html)
- [txiki.js - 使用 libuv 的 JavaScript 运行时](https://github.com/saghul/txiki.js)
- [Node.js 事件循环](https://nodejs.org/en/docs/guides/event-loop-timers-and-nexttick/)