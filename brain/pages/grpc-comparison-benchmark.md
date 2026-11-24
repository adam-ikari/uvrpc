---
id: grpc-comparison-benchmark
title: "与 gRPC 的性能对比口径：同机同法测默认调用路径，不宣称谁更快"
category: decision
status: active
tags: [benchmark, grpc, methodology]
created: "2026-11-24T14:10:50"
updated: "2026-11-24T15:46:23"
---

<!-- compiled_truth -->
## 与 gRPC 对比的口径

只比双方默认调用路径在 loopback TCP 上的顺序 ping-pong：一请求在途、8 字节 echo、1000 预热不计入、5 次 100k 采样取中位数。UVRPC 侧有两条：`perf_benchmark tcp`（回调泵，16555）与 `uvrpc_sync_ping_pong`（阻塞 await，16558）；gRPC 侧用 C++ `grpc_ping_pong`（同步 unary，16557）。`compare_grpc.sh` 同机跑三侧并报主机、样本与峰值 RSS，沿用 [[uv-run-once-benchmark-methodology]] 的前提。不比 INPROC/SAMELOOP（gRPC 无进程内传输）、流水线吞吐、TLS、跨机。结构差异写进结论：gRPC core 有自己的线程池，UVRPC 是单线程单 loop；gRPC 多背 protobuf + gRPC/HTTP2 帧。标题里"不宣称谁更快"针对的是初测那两个受噪声支配的会话，不是修后带证据等级的结论。

## 构建与 CI 门禁

`UVRPC_GRPC_BENCHMARK` 默认 OFF —— 普通构建不依赖 gRPC 工具链；Ubuntu 22.04 装 `libgrpc++-dev protobuf-compiler-grpc protobuf-compiler libprotobuf-dev` 后开 ON。生成的 pb 文件不入库，configure 时用 protoc + grpc_cpp_plugin 从 `benchmark/grpc_proto/echo.proto` 生成。`benchmark.yml` 现在装这套工具链、构建三个二进制、跑 `compare_grpc.sh 100000 5`，再由 `check_grpc_comparison.py` 在缺行、spread > 2.0x、任一 UVRPC 延迟或 RSS 高于 gRPC 时失败（负控已验：缺 sync 行、sync 延迟改 999 us 都被拒）。见 [[pending-ring-size-balance]]。

## 用 gRPC 的调用方式测 UVRPC

`uvrpc_client_call_async()` 阻塞调用线程、内联驱动共享 loop，是 gRPC 同步 unary 的对等物；单独成二进制是为了不破坏 `perf_benchmark` 的基线与 CI 门禁。async block 的 deadline 覆盖整段，固定 5 s 会在 10 万次中途到期，因此按预热实测速率推导（参见 [[uv-run-once-benchmark-methodology]] 的截止时间原则）。干净会话（load 3.9，spread ≤1.1x，5×100k）：回调泵 24.68 us / 2,676 KiB，阻塞 await 25.53 us / 2,972 KiB，gRPC 73.74 us / 11,684 KiB —— 同调用方式下 UVRPC 仍快 2.89x、RSS 低 3.9x。

## 结论（2026-11-24 反转，证据等级：本机实测，非 CI）

初测两会话 load 3.4–4.0：UVRPC TCP 74.64 / 71.99 us，gRPC 64.72 / 70.47 us，差 13%→2% 且区间重叠，当时判为"同量级、无稳定顺序"。该结论被 [[deadline-sweep-index]] 推翻：instruction profile 显示 sweep 占 96.9% 指令，每请求全扫 65,536 槽。

修后同机交错 A/B（load 13.5）UVRPC 75.03 → 27.76 us（-63%）；与 gRPC 同会话交错（load 10.9）UVRPC 27.98 us vs gRPC 72.04 us（2.57x）。峰值 RSS 在 ring 降到 `1<<10` 后为 UVRPC 2,672 KiB vs gRPC 11,788 KiB（4.4x）。load 16、gRPC spread 3.6x 的那次不引用。原始样本与复现命令在 `docs/guide/benchmark.md#uvrpc-vs-grpc`。


## Timeline

- time: 2026-11-24T14:10:50
  kind: decision
  summary: "Created this page: 与 gRPC 的性能对比口径：同机同法测默认调用路径，不宣称谁更快"
  source: "2026-11-24 benchmark/grpc_ping_pong.cc + compare_grpc.sh 实测"
  affects: [grpc-comparison-benchmark]

- time: 2026-11-24T14:10:50
  kind: decision
  summary: "记录 gRPC 对比的口径、构建开关与本机实测结论"
  source: "2026-11-24 docs/guide/benchmark.md + 两次本机会话"
  affects: [grpc-comparison-benchmark]

- time: 2026-11-24T14:10:50
  kind: evidence
  summary: "两次本机会话：UVRPC tcp 74.64/71.99 us vs gRPC 64.72/70.47 us，差距从 13% 缩到 2% 且第二次区间重叠；判为同量级无稳定顺序"
  source: "2026-11-24 ./benchmark/compare_grpc.sh 100000 5 × 2"
  affects: [uv-run-once-benchmark-methodology]

- time: 2026-11-24T14:45:48
  kind: decision
  summary: "反转'同量级无稳定顺序'结论：sweep 索引修复后 UVRPC TCP 比 gRPC 快 2.57x、RSS 低 3.6x"
  source: "2026-11-24 docs/guide/benchmark.md + interleaved A/B"
  affects: [grpc-comparison-benchmark]

- time: 2026-11-24T14:45:48
  kind: reversal
  summary: "反转'与 gRPC 同量级无稳定顺序'：sweep 索引修复后交错实测 UVRPC 27.98 us vs gRPC 72.04 us，RSS 3.6x 更低"
  source: "2026-11-24 callgrind + interleaved A/B + compare_grpc.sh"
  affects: [deadline-sweep-index, uv-run-once-benchmark-methodology]

- time: 2026-11-24T15:16:40
  kind: decision
  summary: "修正 RSS 为 4.4x 并补上 CI 门禁口径"
  source: "2026-11-24 benchmark.yml + pending-ring-size-balance"
  affects: [grpc-comparison-benchmark]

- time: 2026-11-24T15:46:23
  kind: decision
  summary: "新增用 gRPC 同步调用方式测 UVRPC 的第三条对比路径与 2.89x 结果"
  source: "2026-11-24 benchmark/uvrpc_sync_ping_pong.cc + 5x100k 干净会话"
  affects: [grpc-comparison-benchmark]
