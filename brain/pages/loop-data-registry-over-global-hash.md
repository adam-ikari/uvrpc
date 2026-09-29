---
id: loop-data-registry-over-global-hash
title: "INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
category: decision
status: active
tags: [registry, transport, inproc]
created: "2026-09-28T17:08:39"
updated: "2026-09-29T00:32:44"
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
    void*    sameloop_servers;    /* sameloop_server_t*  单链表 */
} uvbus_loop_registry_t;
```

`loop->data` 是 libuv 的**公开字段**，任何人都可能已经用了它。框架的处置是**拒绝而非覆盖**（`:16-19`）：`uvbus_loop_registry_retain()`（`:56-82`）读 `loop->data`，NULL 就建 registry；非 NULL 且 magic 不符就 **返回 NULL 并记日志**，transport 随即报错退出。宁可让 inproc/sameloop 在别人的 loop 上不可用，也不破坏用户状态。测试与示例从不设 `loop->data`（`:15-16`），所以正常路径碰不到这个分支 —— 它是给库使用者准备的。

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
