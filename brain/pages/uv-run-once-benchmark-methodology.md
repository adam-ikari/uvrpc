---
id: uv-run-once-benchmark-methodology
title: "性能数字的测量前提：顺序 ping-pong + UV_RUN_ONCE + 日志必须 OFF"
category: concept
status: active
tags: [benchmark, measurement, methodology]
created: "2026-09-28T17:15:24"
updated: "2026-09-29T03:57:31"
---

<!-- compiled_truth -->
## 这些数字是什么量

README 表里的 "throughput" 是**顺序 ping-pong（单请求在途）往返延迟的倒数**，**不是流水线吞吐**。
把它当并发吞吐引用就是误读。同 Loop 与 INPROC 打平（~4.9 µs / ~205k req/s）是"快路径没有额外开销"的证据，
不是"比 INPROC 快"的卖点。

## 测量前提（四条，缺一数字就不成立）

1. **泵用 `UV_RUN_ONCE`，不是忙轮询**。`pump_until()`（`perf_benchmark.c:52-62`）在
   `received < target` 时循环 `uv_run(loop, UV_RUN_ONCE)`：线程阻塞等事件就绪（真实 libuv 用法），
   且每个响应触发 recv_cb 后 `uv_run` 返回，故必然终止。
   **历史教训**：早先的 `uvrpc_benchmark` 正是泵法不对而挂死，才被 `perf_benchmark` 整体替换。
   写新 benchmark 时不要退回 `UV_RUN_DEFAULT` + 计数退出，也不要忙轮询。
2. **debug 日志必须 OFF，且 benchmark 会自己拒绝出数**。`perf_benchmark.c:66-71` 有硬守卫：
   编译进 `UVRPC_DEBUG`/`UVBUS_DEBUG` 时直接报错"numbers NOT valid, rebuild with
   -DUVRPC_DEBUG_LOGGING=OFF"。因为 trace 宏每帧写 stderr（`include/uvrpc.h:30-31`、
   `include/uvbus.h:34-35`），量级足以淹没 4.9 µs 的往返。**这条守卫是"数字有效性"的一部分，别绕过它。**
3. **预热与计时分离**：启动/连接后各转 10、50 次 `UV_RUN_NOWAIT`，再跑 **1000 次不计入**的 warmup，
   之后才开始计时（`:118-150`）。
4. **deadline 随请求数缩放**：`num_requests/10 + 10000` ms（≈0.1 ms/req，按最慢的 TCP ~50 µs/req 留 2 倍余量 + 5 s 抖动）。
   固定超时的 benchmark 在 1M 次压力下会假失败。

## 结构与负载前提

- **所有传输共用同一个单线程 loop**（`:118-121` 注释）：INPROC/SAMELOOP 必须共享，TCP/IPC 在服务端
  recv_cb 同 loop 下也成立。即"5 个平级传输"（见 [[uvbus-transport-abstraction]]）在 benchmark 里是
  同一 loop 上的对等端，不是跨线程。
- 负载固定 **8 字节**（`uint8_t payload[8]`），method `"echo"`，默认传输 `inproc`，可用
  `./dist/bin/perf_benchmark [请求数] [tcp|ipc|inproc|sameloop]` 覆盖（`:9`、`:76-77`）。
- 构建前提：Release + **system 分配器**（README/CI 的口径），否则 mimalloc 与分配器差异混进对比。

## 复现口径

`./scripts/setup_deps.sh && ./build.sh release system`（构建链见 [[build-distribution-breakage]]），
然后 `./dist/bin/perf_benchmark 50000 <transport>`。CI 的 `benchmark.yml` 就是按这个口径把表刷进 step summary。


## Timeline

- time: 2026-09-28T17:15:24
  kind: decision
  summary: "Created this page: 性能数字的测量前提：顺序 ping-pong + UV_RUN_ONCE + 日志必须 OFF"
  source: "git b404542, 1d8ac59 + benchmark/perf_benchmark.c:40-70"
  affects: [uv-run-once-benchmark-methodology]

- time: 2026-09-29T00:27:42
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "benchmark/perf_benchmark.c 逐行核对（:9 / :52-62 / :66-71 / :118-150）+ README 性能表"
  affects: [uv-run-once-benchmark-methodology]

- time: 2026-09-29T03:57:31
  kind: decision
  summary: "测量预算不再假设每请求成本：由预热 1000 请求的实测速率推（4x + 10s 余量），并加 stall_limit_ms（默认 5s 无进展判为卡死，报 'measure stalled'）。UVRPC_BENCH_BUDGET_MS / UVRPC_BENCH_STALL_MS 可覆盖。旧写法 requests/10+10000 隐含 ≤0.1ms/req，在虚拟化主机上 TCP ~0.4ms/req 会误判超时"
  source: "2026-09-29 实施 + 实测：perf_benchmark 50000 tcp 与 100000 inproc 均正常收尾"
  affects: [uv-run-once-benchmark-methodology]
