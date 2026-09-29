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

| 传输 | 往返延迟 | 吞吐（1/延迟） | 适用场景 |
|---|---|---|---|
| SAMELOOP | 1.00 µs | ~1,000,000 req/s | 同 loop 内调用，vtable 短路（最快） |
| INPROC | 1.02 µs | ~980,000 req/s | 进程内零拷贝 |
| IPC | 10.76 µs | ~93,000 req/s | 本机跨进程（Unix socket） |
| UDP | 20.10 µs | ~50,000 req/s | 可容忍丢包；仅本机 loopback，见下 |
| TCP | 20.08 µs | ~50,000 req/s | 可靠网络 RPC |

重跑该工作流即可复现。绝对值随主机而变，框架真正保证的是**比值**（进程内 ≫ 本机 socket ≫ 网络）。
上表是**那一类 runner** 的回归基线，不是容量承诺；要用于容量评估请先在目标硬件上自测。

**UDP 这一行只是本机 loopback 的数字。** benchmark 是单请求在途的顺序 ping-pong，而库不重传：
丢掉一个数据报，响应就永远不会到，stall 检测器触发，运行以 `measure stalled` 终止 —— 它不会
表现为吞吐下降。所以一次跑完的 UDP 运行只说明链路没丢包，对真实网络没有任何说明力（那里丢包和
延迟同时存在）。程序现在会在打印 UDP 数字之前先往 stderr 输出这段提示；除 loopback 外，
这一行与 TCP 那一行不可比。

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
