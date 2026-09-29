---
id: uv-run-once-benchmark-methodology
title: "性能数字的测量前提：顺序 ping-pong + UV_RUN_ONCE + 日志必须 OFF"
category: concept
status: active
tags: [benchmark, measurement, methodology]
created: "2026-09-28T17:15:24"
updated: "2026-09-29T06:38:08"
---

<!-- compiled_truth -->
## 这些数字是什么量

文档表里的 "throughput" 是**顺序 ping-pong（单请求在途）往返延迟的倒数**，**不是流水线吞吐**。
把它当并发吞吐引用就是误读。SAMELOOP 与 INPROC 基本打平（CI runner 上 1.00 µs vs 1.02 µs）是
"快路径没有额外开销"的证据，不是"比 INPROC 快"的卖点。

**当前基线来自 CI**，不是某台开发机：`Benchmark` 工作流在 GitHub Actions `ubuntu-latest`
runner 上每种传输跑 50,000 次往返（run `36527761470`）——SAMELOOP 1.00 µs、INPROC 1.02 µs、
IPC 10.76 µs、UDP 20.10 µs、TCP 20.08 µs；同批 1,000,000 次压测 SAMELOOP 0.97 µs。
换一台机器绝对值就变（同一构建在 5800H 笔记本上进程内约 2.5–3.2 µs），所以任何引用都必须写明测量源。
不要再往文档里写"某台机器"的数字当基线 —— 上一版的 ~4.9 µs / ~205,000 req/s 就是这么来的，
它比同一构建的真实值差了约一倍，且被复制到 9 个文件里。

## 测量前提（四条，缺一数字就不成立）

1. **泵用 `UV_RUN_ONCE`，不是忙轮询**。`pump_until()`（`benchmark/perf_benchmark.c:60-72`）在
   `received < target` 时循环 `uv_run(loop, UV_RUN_ONCE)`：线程阻塞等事件就绪（真实 libuv 用法），
   且每个响应触发 recv_cb 后 `uv_run` 返回，故必然终止。
   **历史教训**：早先的 `uvrpc_benchmark` 正是泵法不对而挂死，才被 `perf_benchmark` 整体替换。
   写新 benchmark 时不要退回 `UV_RUN_DEFAULT` + 计数退出，也不要忙轮询。
2. **debug 日志必须 OFF，且 benchmark 会自己拒绝出数**。`perf_benchmark.c:91` 有硬守卫：
   编译进 `UVRPC_DEBUG`/`UVBUS_DEBUG` 时直接报错"numbers NOT valid, rebuild with
   -DUVRPC_DEBUG_LOGGING=OFF"。因为 trace 宏每帧写 stderr（`include/uvrpc.h:30-31`、
   `include/uvbus.h:34-35`），量级足以淹没 1 µs 的往返。**这条守卫是"数字有效性"的一部分，别绕过它。**
3. **预热与计时分离**：`WARMUP_REQUESTS`（`perf_benchmark.c:53`，1000 次）不计入；启动/连接后
   各转 10、50 次 `UV_RUN_NOWAIT` 让 loop 稳定（`:141`、`:150`），之后才开始计时。
4. **deadline 从主机实测速率推，不假设每请求成本**（`:182-188`）：
   `num_requests × 预热期实测 us/req × 4 / 1000 + 10000` ms，并且只有"连续
   `stall_limit_ms`（默认 5s，来自 `UVRPC_BENCH_STALL_MS`）无进展"才提前中止并报
   `measure stalled` —— 区分"卡死"和"只是慢"。两者都能用环境变量
   （`UVRPC_BENCH_BUDGET_MS` / `UVRPC_BENCH_STALL_MS`）覆盖。
   **旧写法** `requests/10 + 10000` 隐含"往返 ≤0.1 ms"，在虚拟化主机上 TCP 约 0.4 ms/req
   会误判超时，50,000 次的运行什么都来不及打印就死掉。**写新 benchmark 别退回这个假设。**

## 结构与负载前提

- **所有传输共用同一个单线程 loop**（`:136-141`）：INPROC/SAMELOOP 必须共享，TCP/IPC 在服务端
  recv_cb 同 loop 下也成立。即"5 个平级传输"（见 [[uvbus-transport-abstraction]]）在 benchmark 里是
  同一 loop 上的对等端，不是跨线程。
- 负载固定 **8 字节**（`uint8_t payload[8]`），method `"echo"`，默认传输 `inproc`。
- 构建前提：Release + **system 分配器**（`benchmark.yml` 与 `ci.yml` 的口径），否则 mimalloc 与
  分配器差异混进对比。

## 复现口径

```bash
./scripts/setup_deps.sh
cmake -S . -B build -DUVRPC_ALLOCATOR_DEFAULT=system -DCMAKE_BUILD_TYPE=Release \
      -DUVRPC_DEBUG_LOGGING=OFF
cmake --build build -j$(nproc) --target perf_benchmark
./dist/bin/perf_benchmark 50000 <transport>
```

CI 的 `Benchmark` 工作流就是这串命令，每种传输跑一次并把结果写进 job 日志与 step summary
（两个地方都写，所以 `gh run view --log` 能取到，不必去抓渲染后的运行页）。

**没跑过 IPC/SAMELOOP 的 loop 零初始化陷阱**：libuv 1.47 的 `uv_loop_init()` 保留 `loop->data`，
未零初始化的 `uv_loop_t` 会让这两条传输启动失败 —— 见 [[loop-data-registry-over-global-hash]]。


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

- time: 2026-09-29T05:50:17
  kind: evidence
  summary: "性能表改用 CI 数据（GitHub Actions ubuntu-latest runner，run 36527761470 / commit 9c9aa24）：SAMELOOP 1.00 µs、INPROC 1.02 µs、IPC 10.76 µs、UDP 20.10 µs、TCP 20.08 µs，1M 压测 SAMELOOP 0.97 µs。旧的 ~4.9 µs/~205,000 req/s（作者 5800H 笔记本）在同一构建上实测只有 ~2.5–3.2 µs，即旧表把数字记小了约一倍"
  source: "gh run 36527761470；本机复测 12 核 5800H（load 2.58）(2026-09-29)"
  affects: [uv-run-once-benchmark-methodology]

- time: 2026-09-29T06:38:01
  kind: decision
  summary: "结论按 2026-09-29 的代码与 CI 数据重写：删掉 ~4.9 µs 旧数字，并修正第 4 条测量预算（早已改成按预热速率推算，compiled_truth 还停留在旧写法）"
  source: "benchmark/perf_benchmark.c:53,60,72,91,141,150,160-188 逐行核对；gh run 36527761470 (2026-09-29)"
  affects: [uv-run-once-benchmark-methodology]

- time: 2026-09-29T06:38:08
  kind: reversal
  summary: "反转：性能基线不再是'作者开发机的 ~4.9 µs / ~205,000 req/s'，改用 Benchmark 工作流在 CI runner 上的实测值（run 36527761470）。同时发现 compiled_truth 第 4 条还写着旧的固定 deadline 公式，而代码自 b7f9961 起已改为按预热速率推算 —— 结论与代码不符，本次一并改正"
  source: "gh run 36527761470；benchmark/perf_benchmark.c:182-188 复核 (2026-09-29)"
  affects: [uv-run-once-benchmark-methodology]
