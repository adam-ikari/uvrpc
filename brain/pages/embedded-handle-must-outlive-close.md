---
id: embedded-handle-must-outlive-close
title: "内嵌 libuv handle 的结构体必须活到 close 回调"
category: decision
status: active
tags: [libuv, memory-safety, teardown, valgrind, transport]
created: "2026-10-11T05:24:18"
updated: "2026-10-11T05:37:11"
---

<!-- compiled_truth -->
## 约束

堆上结构体**内嵌** `uv_*_t` handle 时，`uv_close()` 只把 close **入队**：libuv 会继续持有指向该 handle 的指针，直到后续某个 loop turn 真正执行它。因此这种结构体**不能在 `uv_close()` 之后立刻 `free()`**——释放必须挂到 close 回调上。

`uv_close(..., NULL)` 就是放弃这个信号。全库扫过一次 `uv_close(.*NULL)`，共 14 处：**11 处后面紧跟 `uvrpc_free()`**（IPC 6 处 + TCP 5 处），全部是本缺陷；余下 3 处是 primitives（promise / semaphore / waitgroup），它们入队后并不释放宿主，且常规调用方在 `main()` 作用域、循环还会继续转，不在其列。**2026-10-11 修完后，src/ 里已无一处 `uv_close(..., NULL)` 伴随立即释放。**

## 证据（2026-10-11）

两个新门禁（均 valgrind `--error-exitcode=9`）：

- `tests/acceptance/client_teardown.c` → `client_teardown_memcheck`，修前 **51 次 invalid access**，按块分三类：
  - **344/352 字节** = `uvasync_scheduler_t`（semaphore + waitgroup 各嵌 `uv_async_t`）：`uvasync_scheduler_destroy()` 入队后立刻 `UVASYNC_FREE`。`uvrpc_client_free()` 之后**第一次** `uv_run()` 就读它——修前 `await_span` 单测就 88 次。
  - **65,928 字节** = `uvbus_ipc_client_t`（内含 64KB `read_buffer`）：`ipc_disconnect()` 的 client 分支。
  - **304 字节** = `uvbus_ipc_server_t`（内嵌 `listen_pipe`）：同函数的 server 分支。
- `tests/acceptance/listen_failure.c` → `listen_failure_memcheck`，修前 **15 次**（296 字节 = `uvbus_tcp_server_t`）。只覆盖**错误路径**中的 bind 失败：TCP 用「两个 server 抢同一个端口」（EADDRINUSE），IPC 用「socket 路径的父目录不存在」。

三类**行为上完全无症状**：块仍被映射、未被复用，读到的是陈旧但可读的内存。所以从来不会崩，只能靠 valgrind 抓——plain 跑两种状态都是 OK。ASan 在本仓库对这类缺陷是瞎的（libuv 是预编译未插桩的静态库），valgrind 才是门禁。

## 否决过的做法：在 teardown 里"排空 loop"

```c
uv_run(scheduler->ctx->loop, UV_RUN_NOWAIT);   /* 试过，错的 */
UVASYNC_FREE(scheduler);
```

两条独立理由都让它不成立：

1. `uv_run()` 在 `loop->stop_flag != 0` 时**整段 while 被跳过**，而该标志只在函数末尾才清零（`deps/libuv/src/unix/core.c`）。`uvrpc_await()` 经由 `uv_stop()` 把它留成 1，于是排空**恰好在最需要它的地方静默变成 no-op**；Release 下 `await_span` / `span_invariance` 直接 SIGSEGV，Debug 却通过。
2. 标志为 0 时它真的会跑，于是驱动 uvbus 自己排队的 close，而 `uvbus_free()` 还没跑——等于在一个拆到一半的对象上重入 `client_destroy()`。

## 现行做法

**倒计时**：结构体加 `pending_closes`，先 `uv_close(handle, cb)` 再跑原有的 `*_cleanup()`（它们 `uv_is_closing()` 判到已关闭会跳过自己的入队，但记账照做），最后一个 close 回调释放整块。内嵌**两个** handle 就必须倒计时；只挂一个回调，会在第一个 fire 后仍留下第二个指向已释放块。

**owner 怎么恢复：**

- 默认用 **`offsetof`**，不碰 `handle->data`。`data` 指向 semaphore / transport，`semaphore_async_callback()`、`on_server_connection()` 要读它，而 `uv_close()` 到 close 实际执行之间，**已 signalled 的 async / accept 事件仍可能派发**——此时改 `data` 会把 scheduler 当 semaphore 交给回调。IPC 六处走的是这条。
- **例外**：若能证明读 `data` 的那个回调已不可能再触发，就地 re-point 并复用既有回调更省代码。TCP 的 `tcp_disconnect` 早就是这么做的（还留了注释说明时机），本仓库 5 处错误路径沿用同一写法：bind 失败时压根没 listen，listen 失败时 `uv_listen` 已返回错、connection callback 没被 arm，两者都够不到 `on_server_connection()`。

## 遗留

`listen_failure` 只能覆盖 5 个 TCP 错误路径里的 bind 一处；realloc / listen / read_start / accept 四条仍无测试可达，靠的是与 bind 同形的机械改动。

调用方契约：`uvasync_scheduler_destroy()` 之后 `scheduler` 立即不可用，但**它所在的 loop 必须继续转**，否则那块内存永远不释放。


## Timeline

- time: 2026-10-11T05:24:18
  kind: decision
  summary: "Created this page: 内嵌 libuv handle 的结构体必须活到 close 回调"
  source: "2026-10-11 修复 scheduler/IPC 拆解期 UAF"
  affects: [embedded-handle-must-outlive-close]

- time: 2026-10-11T05:25:16
  kind: decision
  summary: "堆结构体嵌 uv_*_t 时不能在 uv_close() 后立刻 free；释放要挂到 close 回调。"
  source: "tests/acceptance/client_teardown.c, src/uvasync.c, src/uvbus_transport_ipc.c"
  affects: [embedded-handle-must-outlive-close]

- time: 2026-10-11T05:37:11
  kind: decision
  summary: "堆结构体嵌 uv_*_t 时不能在 uv_close() 后立刻 free；释放要挂到 close 回调。owner 用 offsetof 恢复，除非能证明读 data 的回调已不可能触发。"
  source: "tests/acceptance/client_teardown.c, tests/acceptance/listen_failure.c, src/uvasync.c, src/uvbus_transport_ipc.c, src/uvbus_transport_tcp.c"
  affects: [embedded-handle-must-outlive-close]
