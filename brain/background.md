---
slug: background
title: Project background
role: project background
updated: "2026-09-23T04:35:08"
---

# Project background

## Why

C 生态里缺少一个**与 libuv 单线程事件循环天然契合**的 RPC 框架。项目早期的候选传输/序列化栈都自带线程模型，与"所有 I/O 都在一个事件循环里串行跑"的目标冲突：历史上连换三轮 —— nanomsg → ZeroMQ + msgpack → NNG + msgpack → 最终定型为 **libuv 原生传输 + FlatCC(FlatBuffers)**（谱系与理由见 [[uvrpc-tech-stack-lineage]]）。

真正要解决的问题：

- **调用语义**的低延迟、小 payload 请求/响应，而非大块数据搬运
- 同一进程内多个服务**共享一个 event loop**（loop 复用）而不产生符号冲突
- 同进程模块间通信不应付出网络栈与序列化代价（INPROC / SAMELOOP）
- 传输协议可换但 API 不变 —— 换协议只改地址前缀

## Goals

- **零线程 / 零锁 / 零全局变量**：库代码不创建线程、不加 mutex/spinlock/atomic、不留 file-scope 可变全局（system/mimalloc 构建下经 `nm` 验证为 0 符号）。这条铁律单独记在 [[zero-threads-locks-globals]]。
- **统一传输抽象**：TCP / UDP / IPC / INPROC / SAMELOOP 五种传输共享同一套 API，上层无感。
- **类型安全 codegen**：从 FlatBuffers `rpc_service` 声明生成服务端 stub 与客户端调用函数，消除手写注册与字符串方法名。
- **可度量的性能**：以 `benchmark/perf_benchmark` 的顺序 ping-pong 数字为唯一口径（SAMELOOP/INPROC ≈ 4.1 µs RTT，方法论见 [[uv-run-once-benchmark-methodology]]）。
- **生产级质量门**：107/107 ctest 通过、ASan 干净（含 server-freed-before-client 的 UAF 场景）、`-Wall -Wextra -Wpedantic` 零警告，由 CI 强制。

## Non-goals

- **不是大数据传输通道。** 大 payload 走对象存储、流式管道或共享内存（`docs/guide/index.md` 明确划界）。
- **不做多线程并发模型。** 不引入线程池、工作线程、后台 worker；扩展并发的手段是多实例 / 多进程，不是多线程。
- **不做跨语言运行时。** 当前只面向 C99 调用方，没有其他语言绑定。
- **不管理 `uv_loop_t` 生命周期。** Loop 注入：创建、`uv_run`、`uv_stop`、`uv_loop_close` 全部由用户负责，库只接受注入进来的 loop 指针。

## Target user

- 用 libuv 写**单线程事件驱动**服务的 C 开发者，需要在服务间做低延迟小消息调用。
- 需要**多服务共享同一 event loop** 的进程内模块化架构（生成代码按服务独立前缀，天然支持 loop 复用）。
- 对性能数字和内存安全有硬要求的团队 —— CI 里跑 ASan + cppcheck + warning-gate + 性能回归。

> **低置信度 / 待确认**：Goals 里的量化成功标准与 Target user 是从 README、`docs/guide/*` 和 `git log` 反推的，未经用户确认；Non-goals 中"不做跨语言"也是推断（`pyproject.toml` 存在，但只打包 Python 侧的 codegen 工具）。请确认或修正。
