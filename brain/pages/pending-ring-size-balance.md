---
id: pending-ring-size-balance
title: "ring 默认值：按并发上限取 1,024 槽，内存降而延迟不变；CI 守住优势"
category: decision
status: active
tags: [memory, performance, ci]
created: "2026-11-24T15:16:07"
updated: "2026-11-24T15:16:07"
---

<!-- compiled_truth -->
## 平衡判据

默认 `UVRPC_MAX_CONCURRENT_REQUESTS = 100`，而 ring 默认曾是 `1<<16`（512 KiB），即每客户端 655 倍于并发上限的空转内存。失败判据：改小 ring 后同机交错 A/B 的中位延迟不得变差（主机噪声约 13%，超过才算回归），且峰值 RSS 必须下降。

## 实测（证据等级：本机交错 A/B，非 CI）

`1<<16` → `1<<10`（512 KiB → 8 KiB）：中位延迟 25.57 vs 26.21 us（2.5% 差值在噪声内，判为无差异），峰值 RSS 3,184 → 2,676 KiB（少约 508 KiB，16%）。gRPC 侧同机峰值 RSS 11,788 KiB，UVRPC 2,672 KiB，约 4.4x 更低。延迟侧此前 sweep 索引修复已占大头，见 [[deadline-sweep-index]]。语义变化：把 `max_concurrent` 提到约一千以上时必须显式提高 ring，否则撞槽返回 CALLBACK_LIMIT；文档已写清。

## CI 门禁

`benchmark.yml` 装 gRPC C++ 工具链，用同一组 Release / system allocator / debug-OFF 标志构建两侧，跑 `compare_grpc.sh 100000 5` 并把表格写进 step summary。`check_grpc_comparison.py` 在缺侧、spread > 2.0x、UVRPC 中位延迟高于 gRPC、或 UVRPC 峰值 RSS 高于 gRPC 时失败。负控：spread 3.6x 的受争用 run 被拒；把 UVRPC 延迟改成 999 us 或 RSS 改成 99999 KiB 分别被对应门禁拒。门禁是单机相对判据，不发布绝对数字；spread 超 2.0x 表示主机争用，需重跑而非判回归。详见 [[grpc-comparison-benchmark]]。


## Timeline

- time: 2026-11-24T15:16:07
  kind: decision
  summary: "Created this page: ring 默认值：按并发上限取 1,024 槽，内存降而延迟不变；CI 守住优势"
  source: "2026-11-24 交错 A/B + benchmark.yml"
  affects: [pending-ring-size-balance]

- time: 2026-11-24T15:16:07
  kind: decision
  summary: "记录 ring 内存/延迟平衡与 CI 门禁口径"
  source: "2026-11-24 benchmark.yml + check_grpc_comparison.py"
  affects: [pending-ring-size-balance]
