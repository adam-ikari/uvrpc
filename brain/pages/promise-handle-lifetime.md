---
id: promise-handle-lifetime
title: "Promise 句柄寿命：uv_close 是延迟的，且 ASan 在本项目对它全盲"
category: concept
status: active
tags: [libuv, promise, memory-safety, valgrind, testing]
created: "2026-10-11T04:13:02"
updated: "2026-10-11T04:13:31"
---

<!-- compiled_truth -->
## 约束

`uv_close(handle, cb)` **不关闭任何东西** —— 它把关闭排进队列，libuv 直到下次 `uv_run()` 才读这个句柄。因此任何含 `uv_async_t`/`uv_timer_t` 的结构体，其内存必须活过"调用方返回之后的至少一次 loop 转动"。

三个历史缺陷是同一个错误的三种后果（commit `0ff646c`）：

1. `uvrpc_promise_destroy()` 在排队关闭后**立刻**释放结构体 → valgrind 报 `Invalid read of size 8 at uv_run`，块在 `uvrpc_promise_destroy` 里 free、在 `uvrpc_promise_create` 里 alloc。56 次读。**从不崩溃**，因为释放的块通常还映射着也没被复用 —— 这是最坏的"没崩"。
2. `uvrpc_promise_all_sync()` / `race_sync()` 把 combined promise 放在**栈上**再 cleanup → 函数返回后帧已死，下一次 `uv_run()` **SIGSEGV**。这条是真崩的。
3. `uvrpc_promise_race()` 等全部参与者回报才释放 context → 输家永不结束就泄漏（实测 24 bytes definitely lost）。race 在第一个回调就已经决定了，没有理由等。

## 为什么 ASan 在这里是瞎的

`deps/libuv/build/libuv.a` 是**预编译且未插桩**的归档。ASan 的影子检查内联在插桩代码里 —— 库代码分配/释放时会给影子内存下毒，但**读取它的 libuv 不做检查**。所以缺陷 1 这种"libuv 读已释放内存"，ASan 全程无感（实测：完整 ASan 套件含 detect_leaks 全绿，valgrind 一跑就 56 处 Invalid read）。

**结论：本项目里 promise/句柄生命周期类缺陷只能用 valgrind 判定，ASan 不构成证据。**

## 门禁怎么搭

- valgrind 跑**纯 C 验收测试**（`tests/acceptance/promise_lifetime.c`），不要跑 gtest：本环境 valgrind 会对 gtest 自己的 `~basic_stringstream` 报 **73 处 mismatched free()**，把真正的几条淹掉。
- ctest 条目 `promise_lifetime_memcheck`（`--error-exitcode=9 --errors-for-leak-kinds=definite`），约 0.5s，值得常驻。
- 配套一个普通跑的 `promise_lifetime`：只有缺陷 2 是普通跑可见的。

三个缺陷逐个回注验证门禁（回注 → 跑 ctest → 确认失败）：

| 缺陷 | 普通跑 | memcheck |
|---|---|---|
| destroy 在关闭前释放 | missed | **caught** |
| sync 组合器栈上 combined | **caught** | **caught** |
| race context 泄漏 | missed | **caught** |

## 细节陷阱

- 把输家的 callback 置 **NULL 是错的**：`promise_async_callback()` 只在 callback 非空时才 `uv_unref()`，而参与者一 settle 就被 `uv_ref` 过。置 NULL 会让输家挂着一个永远没人 drop 的 ref，于是 loop 有已引用句柄却无 pending 事件 → 下一次 `uv_run(UV_RUN_ONCE)` **永久阻塞**（失败会变成挂死，而不是报错）。解法是换成 no-op 函数。
- `uv_close` 不可重复排队（libuv `assert(!uv__is_closing(handle))`），所以 `cleanup()` 之后不能再 `destroy()` 同一个对象。
- 树内**没有** `uvrpc_promise_race` / `uvrpc_promise_all` 的生产调用方，只有 examples 和 tests。


## Timeline

- time: 2026-10-11T04:13:02
  kind: decision
  summary: "Created this page: Promise 句柄寿命：uv_close 是延迟的，且 ASan 在本项目对它全盲"
  source: "0ff646c 修复三处 promise 生命周期缺陷"
  affects: [promise-handle-lifetime]

- time: 2026-10-11T04:13:31
  kind: decision
  summary: "libuv 的 uv_close() 只是排队关闭，句柄内存必须活到 loop 跑完关闭回调为止；本项目里 ASan 对这一类缺陷结构性失明，必须用 valgrind 门禁。"
  source: 0ff646c
  affects: [promise-handle-lifetime]
