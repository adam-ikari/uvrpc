---
id: pending-buffer-as-concurrency-control
title: "并发限流：无锁三层拒绝（槽位满 / max_concurrent / 传输背压）"
category: decision
status: active
tags: [concurrency, backpressure, client]
created: "2026-09-28T17:15:24"
updated: "2026-09-30T09:16:29"
---

<!-- compiled_truth -->
限流全部是**同步返回值**，不是等待队列。因为零线程/零锁（[[zero-threads-locks-globals]]）意味着"满时挂起等待"在这个模型里无处安放：一阻塞就卡死 event loop。所以策略统一为 **fail fast，把决定权交回调用者**。

## 三层拒绝，各自独立（2026-09-29 现状）

| 触发 | 返回码 | 判据 |
|---|---|---|
| 在途请求超配额 | `UVRPC_ERROR_RATE_LIMITED` | `max_concurrent > 0 && current_concurrent + n > max_concurrent` —— **单次调用与批次调用都检查**（`src/uvrpc_client.c:463-468`、`:645-649`） |
| 回调槽位冲突 | `UVRPC_ERROR_CALLBACK_LIMIT` | `pending_callbacks[idx]` 被活跃条目占用（单次 `:484-490`、批次 `:676-681`） |
| 传输背压 | `UVRPC_ERROR_TRANSPORT_BUSY` | `uvbus_send()` 返回 `UVBUS_ERROR_BUFFER_FULL`（`:517-520`、oneway `:594-596`） |

三层解耦是 `e231041 → b06a903`（2026-09 前）定下来的：槽位是**路由资源**、并发额度是**语义配额**、buffer full 是**传输能力**。第一层的"假满"（取模撞槽）见 [[ring-buffer-over-uthash]]。第三层不 flush 而是报错，理由同第一句：不阻塞。

配额语义（现在与文档一致）：`current_concurrent` 计的是**已登记回调、正等最终响应**的请求。oneway 不登记槽位，因此不计、也不受配额约束。批次是整批判定：超配额时一帧都不发出。发送失败会回滚槽位并回收计数，**回滚前检查所有权**（`pending_callbacks[idx]->msgid == msgid`），因为 INPROC/SAMELOOP 会在 `uvbus_send()` 内同步跑完整个往返、槽位可能已被响应处理释放。

服务端另有一条同构拒绝：连接数达上限时直接回错误响应帧（`src/uvrpc_server.c:78-97`）。`max_clients` 传 0 或负数在 `src/uvrpc_server.c:259` 被夹到默认 1024，**无限模式不存在**。

## 2026-09-29 删除的"名不副实"字段

评审时核实出六处只写不读的配置/状态，用户以为开关有效、实际什么都不做。经维护者决定按"删除无用 API"处理，全部移除：

- `uvrpc_config_t`：`performance_mode`、`pool_size`、`timeout_ms`、`pump_interval` 及其四个 setter（`uvrpc_config_set_performance_mode/set_pool_size/set_timeout/set_pump_interval`），连同 `uvrpc_perf_mode_t` 枚举与 `UVRPC_DEFAULT_POOL_SIZE`。
- `uvrpc_client_t`：`performance_mode`、`in_callback`、`generation`、`timeout_ms`、`batching_enabled`、`batch_size`、`max_batch_size`、`pump_interval`、`pump_timer`、`send_pending`。
- `pending_callback_t`：`generation`、`method`、`poll_timer`、`is_polling`、`last_activity`、`client` —— 条目从 208B 降到 24B（见 [[ring-buffer-over-uthash]]）。
- `STREAM_TIMEOUT_MS` 宏、`start_pump_timer()`、`pump_timer_callback()`。
- `uvbus_config_t` 的 `timeout_ms` / `enable_timeout` 与 `uvbus_config_set_timeout()` / `_set_timeout_enabled()`（总线层同样无人读取；`tests/uvbus_test.c` 的 Test 13 就是专门测这两个 setter 的，一并删除）。

保留并真实生效的：`max_concurrent`（配额）、`max_pending_callbacks`（路由表容量，2 的幂）、`max_clients`、`msgid_offset`、`max_retries`（`uvrpc_client_call` 的同步重投循环 `:532-...`，确实读它）。

**顺手消除的一处隐患**：`pump_timer` 从未 `uv_timer_init()`，而 `start_pump_timer()` 会在 oneway 路径上对它 `uv_timer_start()` —— 只要用户把 `pump_interval` 设成非 0，就是对未初始化句柄调用 libuv（UB）。删除该字段时这条路径一起消失，没有单独修。

## 若要重新实现

超时属于调用方语义：`uvrpc_async_*` 用显式 `timeout_ms` 入参起 `uv_timer`（`timeout_ms == 0` 即不超时），`_sync` 与 `_all`/`_any` 生成包装都走这条路。想要 RPC 级超时，应基于这套已存在的机制加，而不是往 config 上再挂一个没人读字段。自动攒批同理：需要的是把 `uvrpc_client_call_batch` 与一个真实的 flush 定时器接起来，先想清楚"槽位何时算占用"。

## 与流式的交互

槽位只在收到**最后一帧**（`type=1`）时释放（`src/uvrpc_client.c:194-199`），`type=2`（ResponseMore）保留槽位。所以一个长 Stream 会持续占用一个回调槽和一个配额名额，流式并发实际受 `max_pending_callbacks` 约束。协议侧语义见 [[minimal-rpcframe-schema]]。


## Timeline

- time: 2026-09-28T17:15:24
  kind: decision
  summary: "Created this page: 并发限流：无锁三层拒绝（槽位满 / max_concurrent / 传输背压）"
  source: "git e231041 → b06a903 (2026-02-19) + src/uvrpc_client.c:525-558, 705-730 现状核对"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-28T17:15:24
  kind: note
  summary: "2026-09-28 核对：三层拒绝已解耦；generation 防回绕机制从未递增，属未完成的空壳"
  source: "src/uvrpc_client.c:74,205,286,534,548 全量 grep"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-29T00:34:09
  kind: decision
  summary: "补齐 compiled_truth：三层拒绝的真实接线、send_pending 无人读、current_concurrent 只在批次路径递增导致漂移"
  source: brain update-truth
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-29T02:25:38
  kind: note
  summary: "第五、六处只写字段：config->pool_size 从未被任何代码读取（无连接池实现）；config->timeout_ms 只在 src/uvrpc_client.c:282 复制进 client->timeout_ms，该字段此后无读取点，RPC 路径不存在超时执行（只有 uvrpc_async_* 用显式入参起 uv_timer）。已同步修正 include/uvrpc.h 的字段与 setter 文档注释，以及 max_clients '0 = unlimited'、max_pending_callbacks 默认值写错的两处注释"
  source: "2026-09-29 文档审计：grep timeout_ms/pool_size 全量核对 src + include"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-29T03:57:16
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-09-29 修复：删除只写字段、配额改为两路径一致（实测 105/105 + demo 1000 请求 99 次退避重试）"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-30T09:16:07
  kind: decision
  summary: "新增请求截止时间（timeout_ms，默认 30s，0 关闭）：待回调槽位带 deadline，过期后归还并以 UVRPC_ERROR_TIMEOUT 通知。**清扫只在取出槽位时触发（即配额检查之前），没有 per-client 定时器** —— 契约是'通知不迟于该客户端的下一次调用'，空闲客户端不会被打断。取舍：定时器会让 uvrpc_client_free 必须走异步句柄关闭（不泵 loop 就关 loop 会拿到 UV_EBUSY），那个生命周期语义比及时通知更难改回来"
  source: "tests/call_timeout_test.c；实测诊断：发完请求空转 400ms 回调不触发，下一次调用才补报 (2026-09-30)"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-30T09:16:29
  kind: evidence
  summary: "缺陷（已修）：pending 槽位只在响应到达时释放，而客户端**没有任何超时/过期/清扫机制**（grep timeout|expire|reap 在 uvrpc_client.c 全空）。UDP 丢一个响应 → 该槽位占住直到客户端断开；累积到配额后表现为 UVRPC_ERROR_RATE_LIMITED，与原因毫无关联。表本身是固定容量环形缓冲（max_pending_callbacks），所以泄漏有上界，但后果是客户端逐渐不可用。另：UVRPC_ERROR_TIMEOUT = -5 早已存在且异步层（uvrpc_async_all/any）在用 —— 不需要新增错误码"
  source: "src/uvrpc_client.c:151（唯一回收路径）、grep 确认无清扫机制；include/uvrpc.h:55 (2026-09-30)"
  affects: [pending-buffer-as-concurrency-control]
