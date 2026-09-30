---
layout: home

hero:
  name: "UVRPC"
  text: "超快速 C99 RPC 框架"
  tagline: "零线程，零锁，零可变全局。进程内往返在数十微秒量级。"
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/quick-start
    - theme: alt
      text: GitHub
      link: https://github.com/adam-ikari/uvrpc

features:
  - title: 🚀 超快速
    details: "基于 libuv 事件循环和 FlatBuffers 序列化。进程内传输的顺序往返在数十微秒量级；Benchmark 工作流会把实测数字连同测量它的硬件一起公布。"
    link: /zh/guide/benchmark
  - title: 🎯 极简设计
    details: "零线程、零锁、零可变全局。所有 I/O 由 libuv 事件循环管理，库代码无文件级可变全局变量。"
    link: /zh/guide/design-philosophy
  - title: 🔌 五种传输，统一 API
    details: "支持 TCP、UDP、IPC、INPROC、SAMELOOP 传输，仅改地址前缀即可切换，服务端/客户端 API 完全一致。"
    link: /zh/guide/api-guide
  - title: 📦 零拷贝
    details: "FlatBuffers 二进制序列化，进程内传输指针直传，最小化内存拷贝。"
  - title: 🔄 循环注入
    details: "支持自定义 libuv loop，多实例独立运行或共享循环，同循环调用零开销。"
  - title: 📚 类型安全
    details: "FlatBuffers DSL 声明服务，生成类型安全的 C 客户端/服务端桩代码，编译时检查。"
    link: /api/generated-api

---

::: tip UVRPC 的定位
UVRPC 是专注于**调用语义**的 RPC 框架——低延迟、小 payload、请求/响应。
它不是大数据传输管道；大数据场景请使用对象存储、流式管道或共享内存。
进程内传输已是零拷贝指针传递，对小 payload 已达最优。
:::

## 快速开始

```bash
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
./scripts/setup_deps.sh          # 构建内置 libuv/flatcc/mimalloc/gtest
./build.sh                        # Release + mimalloc（默认分配器）

# 运行 TCP echo 往返
./dist/bin/simple_server &
./dist/bin/simple_client
```

## 性能

顺序 ping-pong（单请求在途），8 字节负载，Release 构建，单线程，由 [`Benchmark` 工作流](https://github.com/adam-ikari/uvrpc/actions/workflows/benchmark.yml) 在 GitHub Actions runner 上实测。绝对值随主机而变，方法论见 [Benchmark](/zh/guide/benchmark)。

| 传输层 | 往返延迟 | 吞吐量 (1/延迟) | 适用场景 |
|--------|----------|-----------------|----------|
| SAMELOOP | 最快 | 同循环，vtable 旁路 |
| INPROC   | 最快 | 进程内零拷贝 |
| IPC      | 约慢 1.5 倍 | 本地进程间（Unix 套接字）|
| UDP      | 最慢 | 可丢包；仅本机 loopback |
| TCP      | 最慢 | 可靠网络 RPC |

> 延迟数字取决于机器，不亚于取决于库本身：两台镜像相同的 GitHub Actions runner
> 在同一份代码上就差出 2.2 倍。所以这里不发布固定数字。Benchmark 工作流会连同
> CPU、内存、内核和负载一起记录测量主机，并给出 5 次 10 万请求的中位数 —— 读数字
> 必须连同主机一起读，否则视为未经验证。可用
> `./dist/bin/perf_benchmark [requests] [transport]` 复现。

::: warning 关于"吞吐量"
上表"吞吐量"为顺序往返延迟的倒数（单请求在途），**非**流水线吞吐。
运行 `./dist/bin/perf_benchmark [请求数] [传输]` 复现。
:::

## 设计哲学

- **零线程**——所有 I/O 由 libuv 事件循环管理，无线程池、无后台 worker。
- **零锁**——单线程模型使互斥锁、自旋锁、原子操作都不必要。
- **零可变全局**——库代码无文件级可变全局变量（system/mimalloc 构建），所有状态在 context 对象中。

[阅读完整设计哲学 →](/zh/guide/design-philosophy)

## 传输层

| 传输 | 地址 | 何时使用 |
|------|------|---------|
| TCP  | `tcp://host:port` | 可靠网络 RPC |
| UDP  | `udp://host:port` | 高吞吐、可丢包 |
| IPC  | `ipc:///path`     | 本地进程间 |
| INPROC | `inproc://name` | 进程内零拷贝；需要注册表 |
| SAMELOOP | `sameloop://name` | 同循环，vtable 旁路；需要注册表 |

切换传输只需改地址前缀，一行代码。

## 后续

- [快速开始](/zh/guide/quick-start) — 5 分钟教程
- [构建安装](/zh/build-install) — 完整构建说明
- [API 指南](/zh/guide/api-guide) — 完整 API 文档
- [性能测试](/zh/guide/benchmark) — 方法论与数据

UVRPC 采用 MIT 许可证。[在 GitHub 上 Star →](https://github.com/adam-ikari/uvrpc)

