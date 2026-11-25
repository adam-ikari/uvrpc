---
id: grpc-comparison-benchmark
title: "与 gRPC 的性能对比口径：同机同法测默认调用路径，不宣称谁更快"
category: decision
status: active
tags: [benchmark, grpc, methodology]
created: "2026-11-24T14:10:50"
updated: "2026-11-25T14:27:53"
---

<!-- compiled_truth -->
本页结论与口径已随 2026-11-25 的 v0.1.1 发布固定：tag `v0.1.1` 指向 `1b98fd0`，资产 `uvrpc-0.1.1-linux-x86_64.tar.gz` 的 sha256 为 `90f9c44357d2814c368ea9381f3de1eebe0e40d06a9af2b403241b063920e12f`。构建方式：`./build.sh clean && ./build.sh release mimalloc`；207/207 ctest；从 tarball 解压后静态与共享链接 smoke test 均通过，下载件 sha256 与本地一致。共享库 SONAME 仍为 `libuvrpc.so.0`，ABI 与 v0.1.0 兼容，仅版本化文件名变为 `libuvrpc.so.0.1.1`（CMakeLists `VERSION 0.1.1` 与 `SOVERSION 0`）。README 徽章同步指向 v0.1.1。


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

- time: 2026-11-25T14:27:53
  kind: decision
  summary: "补记 v0.1.1 发布：资产、sha256 与验证方式"
  source: 2026-11-25 release v0.1.1
  affects: [grpc-comparison-benchmark]

- time: 2026-11-25T14:27:53
  kind: decision
  summary: "新建 v0.1.1 release（保留 v0.1.0 历史）；版本号同步 CMakeLists 与 README 徽章，资产布局不变、SONAME 不变"
  source: 2026-11-25 gh release create v0.1.1
  affects: [grpc-comparison-benchmark]
