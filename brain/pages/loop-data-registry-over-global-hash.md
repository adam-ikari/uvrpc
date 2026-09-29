---
id: loop-data-registry-over-global-hash
title: "INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
category: decision
status: active
tags: [registry, transport, inproc]
created: "2026-09-28T17:08:39"
updated: "2026-09-29T09:49:58"
---

<!-- compiled_truth -->
INPROC 与 SAMELOOP 是"同进程内按名字找对端"的传输：server 挂一个名字，client 用同一个名字连上去。这份 name→endpoint 状态**放在 `uv_loop_t` 上（`loop->data`），不是进程全局**。这是"零可变全局"这条第一约束（[[zero-threads-locks-globals]]）在实现上最难的一处落点，也是 `4fe8b67`（2026-02-26）以来反复演进的决定。

## 为什么挂在 loop 上

`src/uvbus_loop_registry.h:8-14` 的推理：server transport 与 client transport 由**两份互相独立的 config** 创建，它们唯一共享的值就是那个 `uv_loop_t*`。把 registry 挂到 `loop->data`，两边就能见面 —— **零文件作用域可变全局、零锁**，因为 loop 按设计契约只跑单线程。反过来，如果用进程全局表，多 loop（线程各自跑 loop，例如 [[uv-run-once-benchmark-methodology]] 里的 benchmark 进程外的用法、或库使用者自己起多个 loop）就需要 rwlock，那会直接违反零锁。

**注意：文档里"UVRPC 不占用 `loop->data`"这句话是错的**（`docs/guide/design-philosophy.md` 144-157 / 639-651 区间）。实现**就是**占用 `loop->data`，并用 magic 守卫处理冲突。

## 结构与 magic 守卫

```c
#define UVBUS_LOOP_REGISTRY_MAGIC 0x55524300u   /* 'URC\0' —— src/uvbus_loop_registry.h:38 */

typedef struct uvbus_loop_registry {   /* :42-49 */
    uint32_t magic;
    int      refcount;
    void*    inproc_endpoints;    /* inproc_endpoint_t** 桶数组 */
    void*    sameloop_servers;    /* inproc/sameloop 传输共用的 server 链表头 */
} uvbus_loop_registry_t;
```

`loop->data` 是 libuv 的**公开字段**，任何人都可能已经用了它。框架的处置是**拒绝而非覆盖**（`:16-19`）：`uvbus_loop_registry_retain()`（`:56-82`）读 `loop->data`，NULL 就建 registry；非 NULL 且 magic 不符就 **返回 NULL 并记日志**，transport 随即报错退出。宁可让 inproc/sameloop 在别人的 loop 上不可用，也不破坏用户状态。

## 陷阱：loop 必须零初始化（2026-09-29 实测）

`uv_loop_t loop; uv_loop_init(&loop);` —— libuv 官方文档的标准写法 —— **在 INPROC/SAMELOOP 上必然失败**。原因在 vendored 的 libuv 1.47 `deps/libuv/src/unix/loop.c:30-38`：

```c
int uv_loop_init(uv_loop_t* loop) {
  void* saved_data;
  saved_data = loop->data;      /* 保留：data 是用户的字段，libuv 不碰 */
  memset(loop, 0, sizeof(*loop));
  loop->data = saved_data;      /* 原样写回，未初始化时就是栈上的垃圾 */
```

`data` 是 `uv_loop_t` 的**第 0 个字段**（偏移 0），libuv 从不初始化它。于是未零初始化的 loop 会把栈垃圾当作用户 `loop->data`，magic 守卫判为"别人的指针"→ `uvrpc_server_start()` 返回 `UVRPC_ERROR`（-2），日志是 `loop->data is set to a non-uvrpc pointer`。**结论：使用 INPROC/SAMELOOP 前必须 `uv_loop_t loop = {0};`**，这也是仓库里能跑通的示例（`examples/sameloop_rpc_demo.c`）都这么写、而 `examples/scenario_1*.c`（用 TCP，不走 registry）随手写也不炸的原因。这个坑静默地绑死在 libuv 版本上：若 libuv 哪天改成初始化 `data`，行为会反过来。

## 生命周期：retain / release

`listen()` 与 `connect()` 各 retain 一次，transport `free()` 时 release（`:19-20`）。`release`（`:82-96`）把 refcount 减到 0 时：释放 inproc 桶数组 → **`loop->data = NULL`** → 释放 registry。语义上 registry 的存在期 = "该 loop 上还有至少一个 inproc/sameloop transport"。

两个细节：
- sameloop 链在 release 里**不需要单独释放**，因为每个 transport 在 disconnect/free 时已经摘掉自己的条目（`:88-90` 注释）。也就是说 registry 只持有**列表头**，条目所有权属于 transport。
- `release` 对 `loop->data` 不是自己 magic 的情况直接 return（`:87`），所以误用不会踩到用户指针。

## 两种容器的不对称

- **INPROC：256 桶哈希 + 每桶单链表**。`UVBUS_INPROC_HASH_SIZE = UVBUS_HASH_TABLE_SIZE = 256`（`src/uvbus_transport_inproc.c:41`、`include/uvbus_config.h:43`），djb2 `hash_string`（`:42-49`），桶数组惰性分配（`inproc_buckets_ensure`，`:59-64`）。`inproc_find_endpoint(reg, name)`（`:68-79`）、`inproc_add_endpoint`、`inproc_remove_endpoint`（`:81-99`）**都接收 reg 参数**，没有一个是全局操作。
- **SAMELOOP：一条单链表**，`reg->sameloop_servers` 直接头插（`src/uvbus_transport_sameloop.c:64,76-77,82`）。查找是 **O(服务器数)**，无哈希。这在"同 loop 内几十个服务"的场景无所谓，但它是两者中未做规模化的一条。
- 另有 `endpoint->ref_count`（`src/uvbus_transport_inproc.c:32`）与 `inproc_client_t` 的连接数组（`:103+`，容量翻倍扩容）—— 这些是**端点级**引用计数，与 registry 的 loop 级 refcount 是两套东西，别混。

## 与并发控制的接缝

registry 让"找对端"无锁；`send` 队列满时的拒绝在 RPC 层，见 [[pending-buffer-as-concurrency-control]]。


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
