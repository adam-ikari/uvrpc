---
id: uvrpc-tech-stack-lineage
title: "技术选型谱系：nanomsg → ZeroMQ → NNG → libuv + FlatCC"
category: decision
status: active
tags: [serialization, event-loop, lineage]
created: "2026-09-28T17:08:01"
updated: "2026-09-29T05:27:32"
---

<!-- compiled_truth -->
## 谱系（每步都有 commit 证据，非回忆）

```
nanomsg ──► uvzmq / ZeroMQ + msgpack ──► NNG + libuv 混合 ──► libuv + FlatCC（重写定型）
```

| 阶段 | 证据 commit | 内容 |
|---|---|---|
| nanomsg 起步 | `83f3b31` | "Initial commit with uvzmq and uthash submodules" |
| 转 ZeroMQ | `79ee299`、`9060a2f` | 加 libuv + ZeroMQ 子模块；"Migrate from nanomsg to uvzmq/zeromq with msgpack serialization" |
| ZMQ 抽象层 | `2ef07ab`、`0d2ff6b` | socket type / mode abstraction |
| 转 NNG | `58d313c`、`adff473`、`7dec181`、`3c4eb92` | "Integrate NNG with libuv event loop" → 同步/异步混合 → 试 NNG task 线程 |
| 换序列化 | `242e856`、`882437f` | "Replace msgpack with FlatCC serialization"；DSL 定义 service |
| 定型重写 | `c207fe2`、`6c258de` | "Rewrite UVRPC with libuv + FlatCC"，全部 I/O 走 libuv loop |

## 为什么这条谱系值得记（不可从代码重建）

选型不是"一开始就选对"，而是**三轮收敛**：nanomsg/ZeroMQ/NNG 各被真实实现并试用过。
今天看到的"一切皆 libuv 事件循环、零线程"这条约束，正是**否掉 NNG task-thread 路线（`3c4eb92`）之后**才定下来的。
重新评估依赖或想引入第二个 I/O 后端时，必须先读这段历史，否则会把已经付过学费的弯路再走一遍。

## 顺带立住的两条构建契约（同样有历史）

- **`-fPIC` 不是今天的要求，是 `84a62c0`（"build libuv with -fPIC for shared library support"）就立过的契约**。
  2026-09-28 修分发时又踩了一次同样的坑（`R_X86_64_PC32 ... recompile with -fPIC`），
  说明这条约束在 `setup_deps.sh` 里必须写成显式 `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`，不能靠默认值。
- **FlatBuffers 字段名踩过坑**：`f77d728`（"use correct FlatBuffers field name 'data' instead of 'params'"）——
  与 [[minimal-rpcframe-schema]] 的 `RpcFrame{...,data}` 命名一致，改 schema 字段名要同步所有生成码调用点。


## Timeline

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "Created this page: 技术选型谱系：nanomsg → ZeroMQ → NNG → libuv + FlatCC"
  source: "git 4ebe38a/79ee299/9060a2f/f5f2921/c207fe2（2026-02-09 → 02-13，四天三轮收敛）"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T00:26:59
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "git log 逐条核对（83f3b31 / 9060a2f / 58d313c / 242e856 / c207fe2 / 84a62c0）"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T05:27:32
  kind: evidence
  summary: "缺陷：tools/uvrpcc.py 处理 schema/log_service.fbs 时崩溃（jinja UndefinedError: 'rpc_data'，tools/templates/rpc_common.c.j2:61），生成器吐到一半就死；文档里的手动编译命令还把 rpc_common 写成 .c（实际是 .h）"
  source: "实测：python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc schema/log_service.fbs -o /tmp/genlog (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]
