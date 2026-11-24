---
id: ring-buffer-over-uthash
title: "客户端回调路由：环形缓冲数组取代 uthash"
category: decision
status: active
tags: [performance, data-structure, client]
created: "2026-09-28T17:08:39"
updated: "2026-11-24T15:15:46"
---

<!-- compiled_truth -->
客户端在途请求的回调路由用**数组直接寻址**，不用哈希表：`idx = msgid & (max_pending_callbacks - 1)`。决定于 `bee4207`（2026-02-14），是热路径上最直白的一次"用地址算术换查表"。

## 当前结构与容量

`pending_callback_t` 现在只有 `msgid / callback / ctx / deadline_ms`（旧的 generation / method / poll_timer / is_polling / last_activity / client 全部删除，见 timeline 2026-09-29 reversal；每在途条目 208B → 24B）。client 里 `pending_callback_t**` 每槽一个指针。

2 的幂**不是优化偏好，是正确性前提**：`& (n-1)` 取模只在 n 为 2 的幂时成立。`uvrpc_config_set_max_pending_callbacks()` 校验 `max_pending >= 64 && (n & (n-1)) == 0 && n <= UVRPC_MAX_PENDING_CALLBACKS`，不满足就静默回落到默认值。当前常量（`include/uvrpc.h`）：`UVRPC_DEFAULT_PENDING_CALLBACKS = 1<<10`（1,024 槽 ≈ 8 KiB）、`UVRPC_MAX_PENDING_CALLBACKS = 1<<22`、`UVRPC_MAX_CONCURRENT_REQUESTS = 100`。

2026-11-24 默认从 `1<<16`（512 KiB）降到 `1<<10`：同机交错 A/B 中位延迟 25.57 vs 26.21 us（噪声内无差异），峰值 RSS 3,184 → 2,676 KiB。ring 是路由表不是配额：把 `max_concurrent` 提到约一千以上时要同步提高 ring，否则撞槽返回 `UVRPC_ERROR_CALLBACK_LIMIT`。实测与门禁见 [[pending-ring-size-balance]]。

## 为什么不是 uthash

数组路径只有一次地址算术 + 一次指针解引用；uthash 需要哈希函数、桶游走和每次 `HASH_FIND` 的比较。这里不需要"任意 key 查找"：msgid 本地连续递增，模运算天然铺满数组，key→slot 是纯函数。回程同理：响应帧带 msgid，同一公式定位同一槽。

## 冲突即"满"，不是"扩容"

槽被占时**不阻塞、不重试、不改用别的槽**，直接 `return UVRPC_ERROR_CALLBACK_LIMIT`（fail-fast 符合 [[zero-threads-locks-globals]]）。取模让"满"成为假象：两个 msgid 只要差值是容量的整数倍就撞同一槽，ring 只是上界不是配额。精确限流靠 `max_concurrent`，见 [[pending-buffer-as-concurrency-control]]。

## uthash 并未离开项目

"取代 uthash"仅限客户端在途回调这一条路径。uthash 仍用于 `src/uvrpc_server.c`（method name → handler）和 `src/uvrpc_idmap.c`（网关 msgid ↔ 本地 msgid）。这两处键不在本地控制下、容量不可预知，正是哈希表该待的地方。文档若写"UVRPC 不用 uthash"或"uthash 是必需依赖"都片面。


## Timeline

- time: 2026-09-28T17:08:39
  kind: decision
  summary: "Created this page: 客户端回调路由：环形缓冲数组取代 uthash"
  source: "git bee4207 (2026-02-14) + include/uvrpc.h:88-97"
  affects: [ring-buffer-over-uthash]

- time: 2026-09-29T00:33:19
  kind: decision
  summary: "补齐 compiled_truth：msgid 取模直址数组取代 uthash、容量 2 的幂契约、批次路径两处不一致、uthash 仍在 server/idmap"
  source: brain update-truth
  affects: [ring-buffer-over-uthash]

- time: 2026-09-29T02:27:22
  kind: note
  summary: "纠正文档流传的内存收益数字：'24B vs 80B/条目、省 83%' 不成立。数组是 pending_callback_t** ——每槽只是一个指针（默认 1<<16 槽 ≈ 512KB），每个在途请求另 calloc 一个 208B 条目，其中 152B 是被内嵌的 uv_timer_t 占掉的（而轮询路径本身是死代码，见 [[pending-buffer-as-concurrency-control]]）。真实收益是 O(1) 位与路由与无哈希桶，不是每条目更小；想压到 24B 得把 poll_timer 从条目里移走"
  source: "2026-09-29 实测：按 src/uvrpc_client.c:42-52 的字段定义编译 sizeof 探针 => pending_callback_t=208B, uv_timer_t=152B"
  affects: [ring-buffer-over-uthash]

- time: 2026-09-29T03:57:31
  kind: reversal
  summary: "条目字段被裁到只剩路由需要的三个：generation / method / poll_timer / is_polling / last_activity / client 全部删除（从未有读取点）。208B → 24B 每在途请求，内嵌 uv_timer_t 消失，于是'把 poll_timer 移走才能到 24B'这条建议直接实现了。附带效果：顺序 ping-pong 的 INPROC 往返从 ~23 µs 降到 ~2.5 µs（同一容器、空载），说明每请求 208B calloc 与 timer 字段触碰不是免费的"
  source: "2026-09-29 实施：sizeof 探针重测 pending_callback_t=24B；105/105 ctest + quota 单测通过"
  affects: [ring-buffer-over-uthash]

- time: 2026-11-24T15:15:46
  kind: decision
  summary: "同步已删除的字段与 1<<10 新默认；记录内存/延迟平衡实测"
  source: "2026-11-24 include/uvrpc.h + interleaved A/B"
  affects: [ring-buffer-over-uthash]

- time: 2026-11-24T15:15:46
  kind: evidence
  summary: "默认 ring 1<<16→1<<10：交错 A/B 延迟 25.57 vs 26.21 us（噪声内），峰值 RSS 3,184→2,676 KiB"
  source: "2026-11-24 include/uvrpc.h + 5 对交错 A/B"
  affects: [deadline-sweep-index, pending-ring-size-balance]
