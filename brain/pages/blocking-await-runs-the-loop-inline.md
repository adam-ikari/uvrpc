---
id: blocking-await-runs-the-loop-inline
title: "阻塞式 await：内联跑 loop，而不是 setjmp/longjmp 跳转"
category: decision
status: active
tags: [async, event-loop, libuv, memory-safety, transport-invariance]
created: "2026-10-09T01:11:21"
updated: "2026-10-09T01:11:35"
---

<!-- compiled_truth -->
## 契约

`uvrpc_client_call_async()` 与 `uvrpc_promise_wait()` 都是**阻塞式**等待：它们在自己的栈帧里
驱动事件循环，直到结果就位。回调不跳转，只**停掉 loop**（`uv_stop`），等 `uv_run()` 自己返回，
调用方再检查循环条件。

## 为什么不能从回调里 longjmp

原实现用 `setjmp`/`longjmp` 实现"协程"：响应回调直接 `longjmp` 回等待点。两个后果：

1. **跳过 libuv 的清理路径。** socket 传输的响应是在 `uv__read()` 内部回调进来的，跳出去就绕过了
   `on_client_read()` 末尾的 `uvrpc_free(buf->base)`。实测：每次 await 泄漏 libuv 刚分配的 64 KB 读缓冲
   （TCP/IPC/UDP 各一），而 INPROC/SAMELOOP 不经读路径所以看不出来。
2. **跳过回调自己的收尾**，该路径上任何"回调之后还会执行"的代码都不可依赖。

改成 `uv_stop` 后回调正常返回，libuv 走完读路径、传输释放缓冲，等待侧靠 `uv_run()` 返回后重查条件。
`setjmp.h` 与两个 `jmp_buf` 已从 `uvrpc_async_ctx` 移除。

## 等待循环必须用 UV_RUN_ONCE

`UV_RUN_DEFAULT` 只在 loop 上没有任何被引用句柄时才返回，而一条已连接的客户端就是。
用它等待 = 响应在一次永不返回的 `uv_run()` 内部被收到，等待条件永远没有机会重查。
**这正是本项目 `brain` flow 页早就写下的反模式**，库自己踩了。

实测（缺陷存在时，服务端必定回包）：

| 场景 | `UV_RUN_DEFAULT` | `UV_RUN_ONCE` |
|---|---|---|
| INPROC / SAMELOOP | 返回 | 返回 |
| TCP / IPC / UDP | **永不返回** | 返回 |

两个 loopback 传输不持有 socket 句柄，所以恰好表现得像是对的 —— `examples/async_await_demo.c`
与 `async_chain_demo.c` 用的都是 INPROC，缺陷因此对所有人不可见。

## 关键细节：什么时候 loop 模式才可观测

**响应到达时不可观测。** 回调会 `uv_stop`，无论哪种模式都会返回。带 deadline 时也不可观测 ——
定时器同样只是停掉 loop。所以"服务端会回包"的用例（包括我最初写的那个）**抓不到**这个缺陷。
唯一能分辨的是 `uvrpc_promise_wait()`：它没有回调停 loop 的捷径，循环条件只能靠 `uv_run()` 返回来重查。
`tests/acceptance/await_span.c` 因此断言 promise_wait 这一格。

**"无 deadline + 永不回包"在 socket 传输上无法返回，两种模式都不行**：没有事件，且连接让 loop 保持被引用。
这不是可修的缺陷，是"调用方接受无限等待"的字面含义；库无法区分"慢"和"永不到"。要可返回就必须给 deadline。

## uv_close 是延迟的：句柄不能内嵌在会被 free 的结构里

三处独立的同一类错误，实测都能崩：

- `uvrpc_async_ctx` 内嵌 `uv_timer_t`，而 `uvrpc_async_ctx_free()` 允许在定时器仍武装时调用
  （await 立即成功时 deadline 还在跑）。free 之后 loop 再迭代就写进已释放内存 → 段错误。
  **改为堆分配**，由 close 回调释放。
- `uvrpc_async_all()` / `uvrpc_async_any()` 把 timeout 定时器放在**栈帧**上再 `uv_close`。
  close 被排队，栈帧已返回，loop 下一轮触碰死帧。**改为堆分配 + close 回调释放**。
- 同一文件里 `uv_async_exec()` / `uvrpc_async_timeout()` / `uvrpc_async_cancel_all()` 对同一块内存
  反复 `uv_timer_init()` / `uv_close()`。**改为 arm/stop 复用同一句柄，由 ctx_free 统一关闭**。

测试侧的同款陷阱：`uvrpc_promise_t` / `uvrpc_semaphore_t` / `uvrpc_waitgroup_t` 的 cleanup 都内嵌
`uv_async_t` 并 close 它，所以这三个在 `tests/unit/test_primitives.cpp` 里必须是 **fixture 成员而不是局部变量** ——
否则测试自己段错误。

## Promise.combinator 的上下文必须活到最后一个参与者

`uvrpc_promise_race()` 过去在**第一个**回调里就 `free(ctx)`，而每个被 race 的 promise 都持有该指针：
之后任何一个 settle 都会读已释放内存。实测：N 个 promise、赢家先到、输家后到 → **每个输家一次 use-after-free**。
改为 `remaining` 计数，最后一个参与者释放。`uvrpc_promise_all()` 的提前返回（任一 reject）同理泄漏上下文，
现在所有出口都计数下降并在归零时释放。

## wait() 对未 settle 的 promise 必须报错

`uvrpc_promise_wait()` 过去返回 `promise->error_code`，而 pending 状态下它就是 0 = `UVRPC_OK`。
调用方（正是头文件里的示例）会拿着一份不存在的成功去读结果。现在返回 `UVRPC_ERROR_TIMEOUT`。
`uvrpc_promise_get_result()` 失败时把 out 参数清零 —— 文档示例忽略它的返回值。

## 测试

- `tests/unit/test_primitives.cpp`：17 个用例，promise 状态机 / 组合子 / semaphore 配额归还 / waitgroup。
  这 1000 行此前**零测试**，只靠 examples 兜着。
- `tests/acceptance/await_span.c`：await 语义跨 5 个传输。断言"服务端会回包时 handler 只跑一次"
  （否则重发请求也长得像成功）、字节一致、以及 promise_wait 一格 —— 后者是唯一能分辨 loop 模式的一格。

**回归验证**：把 `uvrpc_promise_wait()` 的循环改回 `UV_RUN_DEFAULT`，`await_span` 在 IPC/TCP 上失败退出 1。

## 与既有约束的关系

零可变全局仍然成立（本次未引入任何 file-scope 可变变量）。跨传输不变性是本项目已有的验收方法论，
这里第一次把它用在**阻塞等待**上而不是只用在请求/响应上 —— 而"哪个传输持有句柄"恰好决定了等待能否返回。


## Timeline

- time: 2026-10-09T01:11:21
  kind: decision
  summary: "Created this page: 阻塞式 await：内联跑 loop，而不是 setjmp/longjmp 跳转"
  source: "2026-10-09 会话：src/uvrpc_async.c + src/uvrpc_primitives.c 全量审计"
  affects: [blocking-await-runs-the-loop-inline]

- time: 2026-10-09T01:11:35
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-10-09 会话：全量审计 src/uvrpc_async.c / src/uvrpc_primitives.c（此前两者在 tests/ 里零命中）"
  affects: [blocking-await-runs-the-loop-inline]
