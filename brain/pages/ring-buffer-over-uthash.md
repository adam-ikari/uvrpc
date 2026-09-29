---
id: ring-buffer-over-uthash
title: "客户端回调路由：环形缓冲数组取代 uthash"
category: decision
status: active
tags: [performance, data-structure, client]
created: "2026-09-28T17:08:39"
updated: "2026-09-29T03:57:31"
---

<!-- compiled_truth -->
客户端在途请求的回调路由用**数组直接寻址**，不用哈希表：`idx = msgid & (max_pending_callbacks - 1)`。决定于 `bee4207`（2026-02-14），是热路径上最直白的一次"用地址算术换查表"。

## 结构与容量契约

```c
typedef struct pending_callback {        /* src/uvrpc_client.c:40-52 */
    uint32_t msgid;          /* 槽位复用，必须回读校验 */
    uint32_t generation;     /* 防回绕 —— 见下方"未完成的 half-feature" */
    uvrpc_callback_t callback;  void* ctx;
    uint64_t last_activity;
    char* method;  uv_timer_t poll_timer;  int is_polling;
    uvrpc_client_t* client;
} pending_callback_t;

pending_callback_t** pending_callbacks;  /* :69 —— 指针数组，槽位各自堆分配 */
int max_pending_callbacks;               /* :70 —— 必须是 2 的幂 */
uint32_t generation;                     /* :74 */
```

2 的幂**不是优化偏好，是正确性前提**：`& (n-1)` 取模只在 n 为 2 的幂时成立。契约在 `src/uvrpc_config.c:105-115` 强制：`max_pending >= 64 && (n & (n-1)) == 0 && n <= UVRPC_MAX_PENDING_CALLBACKS`，不满足就**静默回落**为默认值。常量（`include/uvrpc.h:88-105`）：`UVRPC_DEFAULT_PENDING_CALLBACKS = 1<<16`（65,536）、`UVRPC_MAX_PENDING_CALLBACKS = 1<<22`、`UVRPC_MAX_CONCURRENT_REQUESTS = 100`。

**已发现的文档错误**：`include/uvrpc.h:237` 的字段注释写 "(default: UVRPC_MAX_PENDING_CALLBACKS)"，而真实默认是 `UVRPC_DEFAULT_PENDING_CALLBACKS`（`src/uvrpc_config.c:29`）。两者差 64 倍。

## 为什么不是 uthash

数组路径只有一次地址算术 + 一次指针解引用；uthash 需要哈希函数、桶游走、以及每次 `HASH_FIND` 的字符串/整数比较。更重要的是**这里不需要"任意 key 查找"**：msgid 由本地生成、连续递增、模运算天然铺满数组，所以 key→slot 是纯函数，不需要容器。回程同理：响应帧带 msgid，同一个公式定位同一个槽。

## 冲突即"满"，不是"扩容"

槽位被占且 `existing->generation == client->generation`（即有效在途条目）时，**不阻塞、不重试、不改用别的槽**，直接 `return UVRPC_ERROR_CALLBACK_LIMIT`（`src/uvrpc_client.c:548-556`）。含义有两层：
1. 这是有意的 fail-fast —— 阻塞 event loop 违反 [[zero-threads-locks-globals]] 的模型。
2. 由于取模，**"满"是假的**：两个 msgid 只要差值是容量的整数倍就撞同一槽。所以实际允许在途数远小于 `max_pending_callbacks`，65,536 只是上界不是配额。想精确限流靠 `max_concurrent` 那层，见 [[pending-buffer-as-concurrency-control]]。

响应侧校验 `pending && pending->msgid == msgid && pending->generation == client->generation`（`:205`）：三个条件全过才回调，否则丢弃该帧（对 oneway 的"多余响应"正是这样被静默吸收的，见 [[minimal-rpcframe-schema]]）。

## 未完成的 half-feature（勿当作已实现）

`generation` 设计意图是检测 msgid 回绕后的陈旧槽位，但**实测从未递增**：唯一写点 `client->generation = 0`（`:286`），之后只被读（`:205`、`:548`）和拷贝（`pending->generation = client->generation`，`:534`）。所以"陈旧槽位替换"分支（`:548`）是不可达代码，回绕保护实际不存在。roadmap 切片 6 处理。

批次路径另有两处与主路径不一致（`uvrpc_client_batch_call`，`:745-752`）：
- 索引用 `% max_pending_callbacks` 而非 `& (n-1)` —— 容量为 2 的幂时等价，但它绕过了 bitmask 契约的表达；
- **不设置 `pending->generation`**（calloc 后保持 0）。今天 `client->generation` 恒为 0 所以恰好正确；一旦切片 6 真的开始递增 generation，批次条目会被判为陈旧。修 generation 时必须同步这里。

## uthash 并未离开项目

"取代 uthash"仅限**客户端在途回调**这一条路径。uthash 仍在生产代码里用：`src/uvrpc_server.c:18,32,170,376,400`（method name → handler，`HASH_FIND_STR`/`HASH_ADD_STR`，且查找前有小写化）与 `src/uvrpc_idmap.c:7,52,64,81`（网关 msgid ↔ 本地 msgid 映射，`HASH_ADD_INT`/`HASH_FIND_INT`）。这两处都是"键不在本地控制下、容量不可预知"的映射，正是哈希表该待的地方。**文档若写"UVRPC 不用 uthash"或"uthash 是必需依赖"都是片面的**（`docs/guide/design-philosophy.md:392` 与 `:152,639` 自相矛盾）。


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
