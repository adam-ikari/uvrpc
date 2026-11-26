# 性能测试

UVRPC 提供两个测量程序。两者都是单线程的，这是刻意为之：框架本身不起线程，
一个去开线程的 benchmark 测量的就是库根本做不到的事。

| 程序 | 源码 | 测量内容 |
|---|---|---|
| `perf_benchmark` | [`benchmark/perf_benchmark.c`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/perf_benchmark.c) | 各传输的 request/response 往返延迟与吞吐 |
| `direct_call_benchmark` | [`benchmark/direct_call_benchmark.c`](https://github.com/adam-ikari/uvrpc/blob/main/benchmark/direct_call_benchmark.c) | 裸函数调用开销 —— SAMELOOP/INPROC 的下界 |

**不存在**多线程、多进程、发布/订阅、或百分位（percentile）benchmark。早期文档
描述过一个带 `-t`、`--fork`、`--publisher`、`--latency` 参数并输出 p50/p95/p99
的 `benchmark` 程序。该程序已不存在，那些参数也从来不属于 `perf_benchmark`。

## 构建

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh
./build.sh release
```

两个可执行文件都落在 `dist/bin/`。`./build.sh` 不会打开 `UVRPC_DEBUG_LOGGING`
（默认 `OFF`），这正是测量需要的状态，原因见下方的守护逻辑。

## perf_benchmark

```
./dist/bin/perf_benchmark [requests] [transport]
```

- `requests` —— 往返次数（默认 `100000`）
- `transport` —— `sameloop` | `inproc` | `ipc` | `udp` | `tcp`（默认 `inproc`）

未知传输名以状态码 2 退出。每种传输的地址是写死的
（`benchmark/perf_benchmark.c:101-120`）：`tcp://127.0.0.1:16555`、
`udp://127.0.0.1:16556`、`ipc:///tmp/uvrpc_perf.sock`、`inproc://uvrpc_perf`、
`sameloop://uvrpc_perf`。因此同一时刻只能有一个 TCP/IPC 测试占用对应端口或 socket。

```bash
./dist/bin/perf_benchmark                      # inproc，10 万
./dist/bin/perf_benchmark 100000 tcp           # TCP，10 万
./dist/bin/perf_benchmark 1000000 sameloop     # SAMELOOP 压测
```

输出：

```
transport=sameloop  requests=1000000  received=1000000  elapsed=974 ms  (sequential ping-pong)
  round-trip latency: 0.97 us/req
  throughput (= 1/latency, 1 in-flight): 1033058 req/s
```

这是 `Benchmark` 工作流真实跑出的一行（run `36527761470`）。同一个二进制在后台还在编译时
会读到慢一个数量级的数字 —— 光是争抢就如此。跨机器的绝对值也会因框架管不着的原因不同。
这正是下文那张表被标注成"一台 runner 的数字"而不是"规格"的原因，也是每一次测量都该写清
跑在什么上的原因。

## 这些数字意味着什么 —— 以及不意味着什么

**这里的吞吐是顺序往返延迟的倒数。** 测量循环发一个请求，泵事件循环直到它的响应
到达，才发下一个（`benchmark/perf_benchmark.c:190-213`）——在途请求恒为 1。这
**不是**流水线化的并发吞吐，程序也没有假装它是：输出行里写着 `sequential
ping-pong`，吞吐行写着 `= 1/latency, 1 in-flight`。

要测异步吞吐，必须自己构造流水线：连续发出 N 个请求不等响应，由回调把它们消化完。
你会撞上的上限就是[单线程模型](/zh/guide/single-thread-model)里那三层无锁拒绝。

### 测量前提

有四件事让这组数字可比，且每一件都由代码保证，而不是靠操作者自觉：

1. **用 `UV_RUN_ONCE` 泵循环，而不是空转轮询。** `pump_until()`
   （`benchmark/perf_benchmark.c:59-79`）让线程阻塞到有事件就绪。此前一个用
   `UV_RUN_DEFAULT` 的版本会挂死，因为存活的连接永远不会让循环退出。
2. **必须关掉调试日志。** `UVRPC_DEBUG` / `UVBUS_DEBUG` 在每次 send/recv 都往
   stderr 写东西，其开销远超 RPC 本身。这个可执行文件拒绝在日志开着时安静地给出数字：
   它会打印 `WARNING: library built with debug logging ON — timings are NOT valid`
   （`benchmark/perf_benchmark.c:89-92`）。看到它就重建成 `-DUVRPC_DEBUG_LOGGING=OFF`。
3. **预热不计入计时。** 先跑 `WARMUP_REQUESTS`（1000）个请求
   （`benchmark/perf_benchmark.c:164-178`），计时从其后开始。循环建立也有明确的
   settle 自旋 —— 连接前 10 次、连接后 50 次（`:141`、`:150`）。
4. **时限由主机自己给出，不再假设每请求成本。** 测量阶段的预算是
   `4 × 预热实测速率 × 请求数 + 10 秒`（`benchmark/perf_benchmark.c:184-188`），
   并且只有连续 `stall_limit_ms`（默认 5 秒）收不到任何响应才提前中止 —— 那是真的
   卡死，而不是机器慢。此前的写法把预算硬编码成 `requests/10 + 10000` ms，隐含
   "顺序往返不超过 0.1 ms/req"；在虚拟化主机上 TCP 约 0.4 ms/req，50,000 请求那次
   就以 `measure timeout` 中止且什么都没打印。默认值不合适时用
   `UVRPC_BENCH_BUDGET_MS=<ms>` 指定预算、`UVRPC_BENCH_STALL_MS=<ms>` 改卡死窗口。
   中止时会报 `received N/M` 并非零退出，绝不会悄悄给出一个偏低的数字。

另外两个前提：server 与 client **共用一个 `uv_loop_t`** —— INPROC/SAMELOOP 必须如此，
TCP/IPC 在这个程序里也是共用同一循环（`benchmark/perf_benchmark.c:136-141`）；
payload 是 **8 字节**，所以这些数字描述的是 RPC 帧与循环往返的成本，不是大块数据传输。

## direct_call_benchmark

```bash
./dist/bin/direct_call_benchmark
```

只通过函数指针调用一个函数，没有传输、没有序列化、没有 libuv。把它当作可相减的基线：
若裸调用成本为 `X`、SAMELOOP 为 `Y`，那么 `Y - X` 才是 RPC 帧与循环往返真正向你收取的费用。
上面那张表同批实测：直接调用 0.339 ns、函数指针 0.335 ns、带检查的间接调用 0.362 ns ——
分发开销比一次往返低三个数量级，所以进程内调用的成本在帧编解码与循环换手，不在那一次跳指针。

## 参考数字

Release 构建、`-O2`、system 分配器、单线程、8 字节 payload，每种传输 50,000 次往返，
由 `Benchmark` 工作流在 GitHub Actions `ubuntu-latest` runner 上实测
（run `36527761470`，commit `9c9aa24`）：

### 这里为什么没有绝对数字表

本页早前有一张往返延迟表，标题里还带 run 号，读起来像规格说明。它不是。两台镜像
相同的 GitHub Actions runner 跑同一份代码，SAMELOOP 分别报出 35.84 µs 和 16.42 µs，
**差 2.2 倍**。那些表本身也是错的：把 SAMELOOP 列为最快，而实测它与 INPROC 持平；
把 TCP 与 UDP 拉开 0.02 µs，而两者不可区分。

脱离机器的延迟数字不是这个库的属性，所以不发布。跨主机稳定的是排序：

| 传输 | 相对速度 | 适用场景 |
|---|---|---|
| SAMELOOP、INPROC | 最快 | 进程内，无系统调用 |
| IPC | 约慢 1.5 倍 | 本机跨进程（Unix socket） |
| TCP、UDP | 最慢 | 网络 |

需要可用的数字就触发 `Benchmark` 工作流：它对每种传输采样 5 次，报告中位数与
max/min 离散度，并记录测量主机的 CPU、核数、内存、内核、负载均值和调频策略 —— 写在
run summary 和 job log 里。没有硬件信息的数字应视为未经验证。要做容量规划，请在目标
硬件上实测。

**UDP 这一行只是本机 loopback 的数字。** benchmark 是单请求在途的顺序 ping-pong，而库不重传：
丢掉一个数据报，响应就永远不会到，stall 检测器触发，运行以 `measure stalled` 终止 —— 它不会
表现为吞吐下降。所以一次跑完的 UDP 运行只说明链路没丢包，对真实网络没有任何说明力（那里丢包和
延迟同时存在）。程序现在会在打印 UDP 数字之前先往 stderr 输出这段提示；除 loopback 外，
这一行与 TCP 那一行不可比。

## 与 gRPC 的性能对比

两侧用同一套方法论：loopback TCP 上的顺序 ping-pong（一次只有一个请求在途）、8 字节 echo、1000 次预热不计入计时、每种采样 5 次 × 100k 取中位数。UVRPC 有两条路径：`perf_benchmark tcp`（回调泵，端口 16555）和 `uvrpc_sync_ping_pong`（阻塞 await，端口 16558）；gRPC 侧是 C++ `grpc_ping_pong`（同步 unary，端口 16557）。结构差异写进结论：gRPC core 自带线程池，UVRPC 是单线程单 loop；gRPC 多背一层 protobuf 与 gRPC/HTTP2 帧。不比进程内传输、流水线吞吐、TLS 或跨机调用。

### 初测结论为什么被推翻

最初两次会话（load 3.4–4.0）两侧都在 65–75 µs，差距 13%→2% 且样本区间重叠，当时结论是"同量级、无稳定顺序"。instruction profile 推翻了它：`sweep_expired_pending()` 占 96.9% 的指令——每次调用都全扫默认 65,536 个 pending 槽 reclaim 到期回调。改成常数时间的活期限索引后，同机交错 A/B 的中位延迟从 75.03 µs 降到 27.76 µs（-63%），超时语义不变（`call_timeout_test` 的负控在关掉 sweep 时报 4 项失败，修复后 ctest 206/206 通过）。

### 用 gRPC 的调用方式测 UVRPC

`uvrpc_client_call_async()` 阻塞调用线程、在调用方栈帧里内联驱动共享 loop，是 gRPC 同步 unary 的对等物。它单独成一个二进制，避免改掉 `perf_benchmark` 的基线与 CI 门禁。async block 的 deadline 覆盖整段而非单次调用，固定 5 秒会在 10 万次运行中途到期（返回 -5），因此按预热实测速率推导。

一次干净会话（load 3.9，三行 spread ≤1.1x，5×100k）：

| 方式 | 中位延迟 | 峰值 RSS | 对 gRPC |
|---|---|---|---|
| UVRPC 回调泵 | 24.68 µs | 2,676 KiB | 快 2.99x |
| UVRPC 阻塞 await（gRPC 式） | 25.53 µs | 2,972 KiB | 快 2.89x |
| gRPC 同步 unary | 73.74 µs | 11,684 KiB | — |

内存侧还把默认 pending ring 从 65,536 槽（512 KiB）降到 1,024 槽（8 KiB）：交错 A/B 延迟 25.57 vs 26.21 µs（在主机噪声内），峰值 RSS 少约 508 KiB。提高 `max_concurrent` 超过约一千时要同步提高 ring，否则撞槽返回 `UVRPC_ERROR_CALLBACK_LIMIT`。

### 构建与复现

```bash
sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc protobuf-compiler libprotobuf-dev
cmake -S . -B build-grpc -DCMAKE_BUILD_TYPE=Release \
  -DUVRPC_ALLOCATOR_DEFAULT=system -DUVRPC_DEBUG_LOGGING=OFF \
  -DUVRPC_GRPC_BENCHMARK=ON
cmake --build build-grpc -j$(nproc) --target perf_benchmark uvrpc_sync_ping_pong grpc_ping_pong
./benchmark/compare_grpc.sh 100000 5
```

`UVRPC_GRPC_BENCHMARK` 默认 OFF，普通构建不需要 gRPC 工具链；pb 代码在 configure 时用 protoc + grpc_cpp_plugin 生成，不入库。

### CI 门禁

`Benchmark` 工作流有两道门禁，都只判相对量，不发布绝对数字。**所有性能基线只由该工作流在 GitHub Actions runner 上产生**；`ci.yml` 不再另跑无门禁的性能 job。最新实测见 [英文页的 CI baseline 表](/guide/benchmark#ci-baseline-authoritative)（run `45750463697`，EPYC 9V74，spread 均 1.0x）。下文 5800H 开发机的数字是历史优化证据，不是基线。

**传输基线门禁**：`benchmark/check_baseline.py` 读单一 `benchmark/baseline.json`，只比**同族比值** —— INPROC/SAMELOOP（进程内族）、IPC/TCP 与 UDP/TCP（socket 族），容差 50%；五条传输都要有结果、spread ≤ 2.0x、SAMELOOP 绝对延迟低于 100 µs 的全局上界。跨族比值（SAMELOOP/TCP）**不进门禁**，只打印作参考：它是硬件属性，不是库属性。三次 CI run（EPYC 7763 / Xeon 6975P-C / EPYC 9V45）的同族比值分别落在 0.989–1.020、0.564–0.598、1.023–1.104，跨族却在 0.033–0.104。新 CPU 不阻塞 CI，medians 自动记入 `benchmark/cpu-records/` 留档，不参与判定。

之前把每条传输都跟 SAMELOOP 比，runner 一换 CPU 就误报回归：所有传输同向变化、代码没动，IPC/UDP/TCP 对 SAMELOOP 的比值却偏了 53–56%。按 CPU 各存一份 baseline 能消除误报，但滚动的 `ubuntu-latest` 标签会不断冒出新 CPU，每出一种就得补一份文件。同族比值一份参考就够，仍能抓住单条传输的回归（TCP 改成 60 µs 会让 IPC/TCP 偏 66.5% 而被拒）。代价是整个 socket 族一起变慢时同族比值看不出来 —— 这是"测库而非测机器"换来的，写在这里留痕。

**gRPC 对比门禁**：工作流用同一组 Release / system allocator / debug-OFF 标志构建三个二进制，跑 `compare_grpc.sh 100000 5` 并把表格写进 step summary。`benchmark/check_grpc_comparison.py` 在三行缺行、任一 spread > 2.0x、任一 UVRPC 中位延迟高于 gRPC、或任一 UVRPC 峰值 RSS 高于 gRPC 时失败。负控已验：缺 sync 行、把 sync 延迟改成 999 µs 都被拒。spread 超 2.0x 表示主机争用，要重跑而不是判回归。

证据等级：上面的门禁与 CI 表为 CI 实测（run `45750463697`）；5800H 开发机的 sweep 优化数字为历史证据，load 16、gRPC spread 3.6x 的那次不引用。详见 [英文页](/guide/benchmark#uvrpc-vs-grpc) 的原始样本。

## 故障排查

```bash
lsof -i :16555                 # 端口被另一次运行占用
rm -f /tmp/uvrpc_perf.sock     # 残留 IPC socket（程序自己也会 unlink）
ps aux | grep perf_benchmark
```

如果 `received < requests`，说明这次运行撞到时限或传输错误并非零退出，数字根本没被
打印。不要拿残缺的运行结果做对比。

## 相关文档

- [设计哲学](/zh/guide/design-philosophy)
- [单线程模型](/zh/guide/single-thread-model)
- [Architecture](/architecture/)（仅英文）
