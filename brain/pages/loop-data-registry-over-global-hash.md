---
id: loop-data-registry-over-global-hash
title: "INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
category: decision
status: active
tags: [registry, transport, inproc]
created: "2026-09-28T17:08:39"
updated: "2026-11-21T08:28:26"
---

<!-- compiled_truth -->
INPROC 与 SAMELOOP 是"同进程内按名字找对端"的传输。name→endpoint 状态现在放在**调用方创建并显式传入的 `uvbus_loop_registry_t`** 里（`uvbus_loop_registry_new()` / `uvbus_config_set_loop_registry()` / `uvrpc_config_set_loop_registry()`），框架不读写 `uv_loop_t::data`。这份对象是 server 与 client 两份独立 config 唯一需要共享的东西，因此不需要进程全局表，也不需要锁，仍满足 [[zero-threads-locks-globals]]。

## 当前契约

调用方建一份 registry，传给所有需要互相发现的 server/client config，并在 transport 全部释放后调用 `uvbus_loop_registry_free()`。未传 registry 时，INPROC / SAMELOOP 的 `listen()` 直接报错并指名创建方法；没有隐藏的 per-loop 或进程级回退。TCP / UDP / IPC 不需要 registry。

## 为什么不再挂 loop->data

早期版本把 registry 挂在 `loop->data`（libuv 唯一的 per-loop 用户字段），用 magic tag 区分自己的对象与用户数据。问题是判断 magic **必须解引用这个用户指针**：libuv 1.47 的 `uv_loop_init()` 会保存并写回 `loop->data`（`deps/libuv/src/unix/loop.c:30-38`），所以按 libuv 文档写的 `uv_loop_t loop; uv_loop_init(&loop);` 会把栈残留留在该字段；实测约五次里有一次在读 magic 时段错误，而不是干净地返回错误。2026-09-29 反转：`tests/loop_data_contract_test.c` 被 `tests/loop_registry_test.c` 取代，14 个示例、benchmark 与 5 个测试全部迁移。详见时间线里的 reversal 条目；`docs/guide/design-philosophy.md` 与 `docs/architecture/index.md` 已是当前契约。

## 结构与生命周期

```c
#define UVBUS_LOOP_REGISTRY_MAGIC 0x55524300u  /* 'URC\0' —— src/uvbus_loop_registry.h */

struct uvbus_loop_registry {
    uint32_t magic;
    int      refcount;
    void*    inproc_endpoints;   /* inproc_endpoint_t* 链表头 */
    void*    sameloop_servers;   /* sameloop_server_t* 链表头 */
};
```

magic 放在首字段，是为了让 `uvbus_loop_registry_is_valid()` 在**调用方已经拥有的指针**上做校验，不再解引用野地址。transport 在 `listen()` / `connect()` 各 retain 一次，`free()` 时 release；refcount 归零时释放 inproc 桶数组并释放 registry 本身。sameloop 链表由 transport 自己在 disconnect/free 时摘掉条目，registry 只存链表头。`release()` 对无效指针直接返回。

## 两种容器

- **INPROC**：256 桶哈希 + 每桶链表（`UVBUS_INPROC_HASH_SIZE = 256`），djb2 `hash_string`，桶数组惰性分配。查找/增删都接收 `reg` 参数，无全局操作。
- **SAMELOOP**：一条单链表，头插、查找 O(服务器数)。同 loop 内服务数量通常很小，未做规模化。
- `endpoint->ref_count` 与 `inproc_client_t` 的连接数组属于**端点级**引用与容量管理，和 registry 的 transport 级 refcount 是两套东西，不要混。

registry 让"找对端"无锁；发送队列满时的拒绝在 RPC 层，见 [[pending-buffer-as-concurrency-control]]。


## Timeline

- time: 2026-09-28T17:08:39
  kind: decision
  summary: "Created this page: INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
  source: "git 4fe8b67 → 7c72b6d + src/uvbus_loop_registry.h 头注释"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:39
  kind: decision
  summary: "4fe8b67 (2026-02-26) 移除全局变量、改静态注册表；后演进为 per-loop loop->data 方案"
  source: git 4fe8b67
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:39
  kind: note
  summary: "docs/guide/design-philosophy.md 仍写着'不占用 loop->data'，与实现矛盾，待切片 2 修正"
  source: "brain:stack Open item 7"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-29T00:32:44
  kind: decision
  summary: "补齐 compiled_truth：loop->data 挂载契约与 magic 守卫、retain/release 生命周期、inproc 256 桶 vs sameloop 单链表"
  source: brain update-truth
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-29T05:23:20
  kind: evidence
  summary: "坑：libuv 1.47 的 uv_loop_init() 故意保留 loop->data（saved_data 存回），所以未零初始化的 uv_loop_t 会让 INPROC/SAMELOOP 直接启动失败"
  source: "deps/libuv/src/unix/loop.c:30-38（实测 0xabab…abab 保留）；tests/allocator_ownership_test.c 首次运行踩到 (2026-09-29)"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-29T05:23:32
  kind: decision
  summary: "补上 uv_loop_init() 保留 loop->data 的陷阱：非零初始化的 loop 会让 INPROC/SAMELOOP 启动失败"
  source: "deps/libuv/src/unix/loop.c:30-38 实测 (2026-09-29)"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-29T08:53:05
  kind: evidence
  summary: "查清了一个此前被文档掩盖的硬限制：判断 loop->data 是不是我们的注册表，必须解引用它读 magic。而 libuv 1.47 的 uv_loop_init 保留该字段，所以未零初始化的 uv_loop_t 可能留下野值 —— 那次读取直接段错误，不是干净失败。文档原先写'干净地失败'是错的，已改为写明限制：框架保证'不覆盖用户数据'，不保证'数据不可读时给你一个错误'"
  source: "tests/loop_data_contract_test.c 编写时实测：poison=0xDEADBEEF 与 memset 0xCD 的 loop 都在 uvbus_loop_registry.h:69 的 magic 读取处 SIGSEGV；有效但非本框架的指针则正常拒绝且不覆盖 (2026-09-29)"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-29T09:49:58
  kind: reversal
  summary: "反转：注册表不再挂 loop->data，改由调用方创建并显式传入。API：uvbus_loop_registry_new() / uvbus_config_set_loop_registry() / uvrpc_config_set_loop_registry() / uvbus_loop_registry_free()；两个 transport 改用 transport->registry；未传注册表时 listen 直接报错并指名创建方法。理由：magic 守卫必须解引用用户指针，未零初始化的 uv_loop_t 实测约 20% 会在读取时段错误。代价：调用方多两行并自管生命周期。全部 14 个示例、benchmark 与 5 个测试已迁移"
  source: "tests/loop_registry_test.c 替换 loop_data_contract_test.c；107/107 通过 (2026-09-29)"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-30T03:15:29
  kind: note
  summary: "注册表改为调用方传入后，全站文档已同步：quick-start（中英）与 api/index 补上注册表用法与'切换传输不再只改一行'的例外说明；guide/index（中英）加注；首页传输表标注'需要注册表'；development/migration.md 新增该破坏性变更的迁移小节（含旧/新代码对照）；examples/INPROC_README 修掉 125K/87K/92K ops/s 这批与 CI 基线矛盾的旧数字（换成 run 36658530099 实测的往返延迟）并修掉指向不存在的 docs/API_GUIDE.md 的死链接。路线图第 5 项随之关闭"
  source: "grep 全库复查：无残留把 loop->data 当挂载点的表述（除 design-philosophy 里解释为何不用的那段）(2026-09-30)"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-30T09:51:11
  kind: evidence
  summary: "[已移至 uvbus-transport-abstraction 页：TCP handle->data 类型混淆] 缺陷（已修，src/uvbus_transport_tcp.c:629）：**TCP 传输的 handle->data 被两种类型读** —— on_server_connection:251 当 uvbus_transport_t* 读（正在监听），on_server_close:357 当 uvbus_tcp_server_t* 读（关闭后）。一个指针满足不了两处。后果：tcp_free 先释放 transport，随后的关闭回调把已释放内存当 server 用 —— heap-use-after-free。修法照抄 UDP（452/461/583 本来就是关闭前把 data 改指 server）。关键教训：**UDP 做对了、TCP 做错了，而差异只能靠读源码发现，没有测试能抓到** —— 这正是跨度不变性要覆盖的东西。IPC 同样的写法（listen_pipe.data = transport）目前无害，因为 uv_close 一律传 NULL 回调"
  source: "ASan + custom 分配器下 span_invariance 段错误；gdb/ASan 栈定位（2026-09-30）"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-11-21T08:28:26
  kind: decision
  summary: "反转后事实：注册表由调用方创建并传入 config，框架不读写 loop->data；保留反转理由、结构与生命周期"
  source: "src/uvbus_loop_registry.h + include/uvbus.h + commit 9e6d8a6"
  affects: [loop-data-registry-over-global-hash]
