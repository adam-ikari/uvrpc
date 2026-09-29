---
id: zero-threads-locks-globals
title: "零线程、零锁、零可变全局：贯穿全项目的第一约束"
category: concept
status: active
tags: [constraint, concurrency, architecture]
created: "2026-09-28T17:08:01"
updated: "2026-09-29T00:13:07"
---

<!-- compiled_truth -->
## 法则表述（准确版）

第一约束是「零线程 / 零锁 / **零可变**全局」——注意是"可变"全局，不是"任何"全局。
`docs/guide/design-philosophy.md` 第 125-153 行的表述偏松（写成"零全局变量"，并把 INPROC 全局端点表列为合法例外），
与代码现状不符，见下。

## 实测结论（2026-09-29，库范围 = src/ + include/，19 个 .c 全量）

- **零线程**：`pthread_create` / `uv_thread_*` / `std::thread` / `uv_queue_work` / TLS / `signal|sigaction` **全部零命中**。
  连 libuv 自带工作线程池都未触发（不用 queue_work / fs_*）。
- **零锁**：`pthread_mutex|spin|rwlock|cond`、`uv_mutex|uv_sem|uv_key`、`atomic_*|_Atomic`、`sem_*`、`volatile` **全部零命中**。
- **零可变全局 = 成立，且可证**：用符号表判定，不靠 grep 猜。
  `nm libuvrpc.a` 全库仅 5 个本地数据符号，全为 `*_vtable`；
  `objdump -t` 显示它们落在 **`.data.rel.ro.local`** —— 即 `static const`、重定位后只读
  （PIE 构建里 const 带函数指针也会归入 `.data.rel.ro`，`nm` 因此标成 `d`，**这不代表可变**）。
  故真·可变 file-scope 全局 **= 0**。

## 唯一被允许的可变全局：custom 分配器

`src/uvrpc_allocator.c` 的 `g_custom_allocator` 是全库唯一的可变 file-scope 变量，受
`#if UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_CUSTOM` 隔离——system/mimalloc 构建里它不存在。
custom 构建中亦仅由 `uvrpc_allocator_init()` 写一次、之后只读。**属文档化的分层例外，不算违反。**

## 其余原则的同批实测

- **循环注入(#5) 成立**：`uvasync_context_create(loop)` 是注入路径(`owns_loop=0`)；
  `uvasync_context_create_new()` 自建 loop(`owns_loop=1`) 是显式 opt-in，且 `uv_loop_init` 不起线程。
- **零拷贝(#3) 成立**：inproc/sameloop 发送路径唯一 `memcpy` 是 `uvbus_transport_inproc.c:140` 拷贝**客户端指针数组**
  （为广播时安全迭代），负载仍是指针传递。
- **统一传输抽象(#7) 成立**：5 个驱动共用一份 `uvbus_transport_vtable_t`，const 分发。
- **极简/零冗余(#1) 曾被违反、已修**：存在一整层 pre-uvbus 死代码（`src/uv_transport.c`、
  `src/uv_transport_tcp.c`、`include/uv_transport.h`、`src/uv_frame.c`、`include/uv_frame.h`、
  `src/uvrpc_khash.h`）——除彼此外零引用、不被任何 CMakeLists 收录、`uv_transport.h` 还声明了无实现的
  udp/ipc/inproc 构造器。2026-09-29 删除，删后 `src/*.c` 与 `UVRPC_SOURCES` 由 22≠19 变成 19:19 完全对账。

## INPROC 全局端点表：文档陈旧（待切片 2 修）

`design-philosophy.md:144-153` 称"INPROC 是唯一使用全局变量的传输，靠全局端点表 `g_endpoint_list`"。
**代码里 `g_endpoint_list` 已不存在**——端点查找已改为 `inproc_find_endpoint(uvbus_loop_registry_t* reg, …)`，
状态挂在 per-loop registry 上（见 [[loop-data-registry-over-global-hash]]）。即实现比哲学文档更合规。


## Timeline

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "Created this page: 零线程、零锁、零可变全局：贯穿全项目的第一约束"
  source: "git 证据链 7c72b6d / ecff4de / 4fe8b67 + CLAUDE.md 与 PRODUCTION_ITERATION_2026-07-13.md"
  affects: [zero-threads-locks-globals]

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "7c72b6d (2026-07-24) 消除库内最后一批 file-scope 可变全局；分配器分发改为编译期宏"
  source: git 7c72b6d
  affects: [zero-threads-locks-globals, loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "ecff4de (2026-06-07) primitives/uvasync 删除全局 mutex、atomics、volatile 与 usleep 轮询"
  source: git ecff4de
  affects: [zero-threads-locks-globals]

- time: 2026-09-29T00:13:07
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-09-29 设计哲学评审：符号表法 + 源码法双路实测 src/ include/"
  affects: [zero-threads-locks-globals]
