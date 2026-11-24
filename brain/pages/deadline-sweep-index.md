---
id: deadline-sweep-index
title: "到期回调扫描：用活期限索引取代每次全扫 65536 槽"
category: decision
status: active
tags: [client, timeout, performance]
created: "2026-11-24T14:45:35"
updated: "2026-11-24T14:45:35"
---

<!-- compiled_truth -->
## 问题与判据

`uvrpc_client_call_no_retry_internal()` 每次调用 `sweep_expired_pending()`，而默认
`max_pending_callbacks = 1<<16`。即使没有到期项，也全扫 65,536 个槽。callgrind 显示该函数占
**96.9% 指令**（200 次 tcp 请求，576M / 595M Ir）；把 sweep 关掉后同机 tcp 从约 70 us 降到约 24 us。
失败判据写死为：同机 5×100k 交错 A/B 的中位数降幅 ≥ 20%（大于已记录的约 13% 主机漂移）。

## 修法（最小机制）

在 `uvrpc_client_t` 里加两个字段：`sweep_active_deadlines`（活期限数）与
`sweep_min_deadline_ms`（最早期限；UINT64_MAX=无、0=未知待校验）。所有取/放槽路径统一走
`pending_slot_take()` / `pending_slot_release()`，插入时 O(1) 更新索引，释放最早项时把最小值置 0
强制下一次校验扫描。sweep 入口先查 `active==0` 或 `min>now`，不满足才做原来的全环扫描，并在扫描中重算最小值；
批量满 32 提前退出时最小值置 0。语义不变：仍先全部摘链再调回调，支持回调重入。

## 验证与证据等级

- 负控：把 sweep 改成直接 return，`call_timeout_test` 报 4 项失败（正常版 OK）。这是"测试能拦住东西"的证据。
- 回归：system allocator、Release、`UVRPC_BUILD_TESTS=ON`，ctest **206/206** 通过。
- 实测（证据等级：本机实测，交错 A/B，非 CI）：old 75.03 us → new 27.76 us，中位数 -63%；
  与 gRPC 同会话交错 27.98 vs 72.04 us（2.57x），峰值 RSS 3,180 vs 11,488 KiB（3.6x）。
- 未采纳：`uv_try_write` 快路径单独测仅约 5%，增加部分写/所有权复杂度，按奥卡姆不叠加第二个机制。
- 未跑：CI runner 数字；本机 sandbox-001 虚拟化共享，load 曾 16 且 gRPC spread 3.6x，那次不用。


## Timeline

- time: 2026-11-24T14:45:35
  kind: decision
  summary: "Created this page: 到期回调扫描：用活期限索引取代每次全扫 65536 槽"
  source: "2026-11-24 callgrind + interleaved A/B"
  affects: [deadline-sweep-index]

- time: 2026-11-24T14:45:35
  kind: decision
  summary: "记录 sweep 索引修复的判据、负控与实测收益"
  source: "2026-11-24 src/uvrpc_client.c + docs/guide/benchmark.md"
  affects: [deadline-sweep-index]

- time: 2026-11-24T14:45:35
  kind: evidence
  summary: "callgrind: sweep_expired_pending 占 96.9% 指令；关掉后 70→24 us；交错 A/B 修后 -63%"
  source: "2026-11-24 valgrind callgrind + /tmp/opencode/perf_nosweep"
  affects: [deadline-sweep-index]

- time: 2026-11-24T14:45:35
  kind: evidence
  summary: "负控：sweep 直接 return 时 call_timeout_test 4 项失败；修复后 ctest 206/206 通过"
  source: 2026-11-24 build-grpc ctest
  affects: [deadline-sweep-index]
