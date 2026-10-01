---
id: zero-threads-locks-globals
title: "零线程、零锁、零可变全局：贯穿全项目的第一约束"
category: concept
status: active
tags: [constraint, concurrency, architecture]
created: "2026-09-28T17:08:01"
updated: "2026-10-01T15:35:22"
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

- time: 2026-10-01T15:10:28
  kind: reversal
  summary: "零可变全局从「有例外」改为「无条件」，已达成并加守护测试（2026-10-01）。原先存在两个例外：(1) g_custom_allocator —— 可插拔自定义分配器的函数指针表，仅在 CUSTOM 构建存在，被 uvrpc_allocator_init() 运行时写入；(2) done.0 —— 我自己今天在 ignore_sigpipe_once() 里加的一次性守卫。用户明确：零全局要严格遵守，「唯一的全局变量」这个例外不成立。处理：删除整个 CUSTOM 路径（不是用条件编译藏起来）—— 它没有任何使用者、测试或 CI 覆盖，allocator_ownership_test.c 虽注册了池分配器但跑在 system 构建里、请求被拒绝、从未生效。uvrpc_allocator_init() 签名收窄为只收类型。done.0 直接删除：signal() 幂等，单线程下守卫无正确性价值，省一次 syscall 而非引入全局。新增 tests/acceptance/no_globals.c 守护测试：读编译后的目标文件（$<TARGET_OBJECTS:uvrpc>），用 objdump 判定符号是否落在 .data/.bss；.data.rel.ro* 与 .rodata 视为只读（5 个传输 vtable 就在 .data.rel.ro.local，重定位后只读）。这个测试本身踩了两个坑，都靠负控制抓出来：(a) 抓不住违规比没有检查更糟 —— 最初它对违规代码报 PASS，因为 $<TARGET_OBJECTS:uvrpc> 展开成分号连接的单个参数被当成一个路径，且 objdump 对不存在的文件静默返回空导致「无符号」也被判通过；现在按分号拆分并要求每个目标必须产出符号表。(b) 解析格式错 —— objdump -t 字段是 地址 标志 O 段名 大小 名称，我最初把段名当十六进制大小解析。负控制验证：把 static int done 加回 uvbus.c，测试正确报 done.0 (4 bytes) is in .bss 并失败。判据：任何检查约束的测试，都必须先证明它在约束被破坏时会失败。"
  source: "nm/objdump 扫描 uvrpc 目标文件；负控制两次失败后通过（2026-10-01）"
  affects: [zero-threads-locks-globals]

- time: 2026-10-01T15:35:22
  kind: reversal
  summary: "删除 CUSTOM 分配器时判断失误：它并非「没有使用者」（2026-10-01）。我只 grep 了 uvrpc_allocator_init / UVRPC_ALLOCATOR_CUSTOM / uvrpc_custom_allocator_t，而 CI 里那个 job 传的是 CMake 字符串 custom，所以没被匹配到 —— 删掉能力后 CI 的 Custom allocator build 在配置阶段直接失败。**判据：删除一个能力之前，要找的是「谁依赖它」，不是「谁调用了它的符号」——构建配置、CI 任务、文档示例都是使用者，符号搜索看不到它们**。代价必须说明白：那个 job 是仓库里唯一能捕获跨堆释放的构建（自定义池只认自己分配过的指针），能力删除后该检测一并消失；mimalloc 构建不是等价替代，它不刻意构造跨堆场景。现在这条规则（flatcc 缓冲区用 flatcc_builder_aligned_free，不能用 uvrpc_free）只由 docs/architecture/integration.md 和 uvrpc_free_encoded() 约束，没有自动化守护。另：同一个提交里我还漏看了 CI 里的 custom job，说明「逐个 grep 确认没有使用者」这种验证方式本身就是不可靠的，应该直接问「这个能力的删除会让什么失败」再动手。"
  source: "CI 配置阶段失败：CMakeLists.txt:49 Invalid allocator type（2026-10-01）"
  affects: [zero-threads-locks-globals]
