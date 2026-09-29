---
id: uvrpc-tech-stack-lineage
title: "技术选型谱系：nanomsg → ZeroMQ → NNG → libuv + FlatCC"
category: decision
status: active
tags: [serialization, event-loop, lineage]
created: "2026-09-28T17:08:01"
updated: "2026-09-29T08:10:47"
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

- time: 2026-09-29T05:32:19
  kind: evidence
  summary: "已修：generate_common_code() 漏传 rpc_data（client.c 有、它没有），模板里 struct-array 分支引用 rpc_data['tables'] 直接抛 UndefinedError。6 个 schema 现在全部生成成功，且对原本能生成的 schema 输出逐字节一致"
  source: "tools/uvrpcc.py:300；实测 schema/*.fbs 全量生成 + /tmp/before vs /tmp/after diff (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T05:32:19
  kind: evidence
  summary: "仍坏（未修）：生成结果本身编译不过 —— 模板给结构体定义的类型名带 namespace 前缀（log_LogEntry_t），字段引用却不带（LogEntry_t*），位置 tools/templates/rpc_common.h.j2:40/59 vs :33/52；examples/log_service_demo.c 也落后于现 API（uvrpc_request_send_response 现返回 void，demo 仍按返回值用）"
  source: "实测 gcc 编译 /tmp/g_log_service 产物 (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T05:40:08
  kind: evidence
  summary: "已修一半：rpc_common.h.j2 声明结构体字段时按表类型加命名空间前缀（log_LogEntry_t），此前是裸 LogEntry_t —— 但 flatcc 0.6 连枚举也带前缀（log_LogLevel_enum_t），所以 log_service 仍编译不过"
  source: "tools/templates/rpc_common.h.j2 + uvrpcc.py:281；其余 5 个 schema 产物逐字节不变 (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T07:07:34
  kind: evidence
  summary: "结构体数组路径的运行时缺陷（此前因生成器崩溃而从未被执行过）：① 向游离向量再 add 偏移，编译通过但解码长度恒为 0（flatcc 0.6 要求在字段作用域内 <Table>_<field>_start/push_create/_end）；② 元素里的字符串字段直接把 const char* 传给 _create 的 string_ref_t 形参，读取时解引用垃圾崩溃。两处都在 client.c.j2 / rpc_common.c.j2。修好后 log_service_demo 端到端跑通：批量 3 条、10 条高频 quick log 全部送达"
  source: "纯 flatcc 往返复现 + examples/log_service_demo.c 端到端实测 (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T08:02:22
  kind: evidence
  summary: "生成器修正收尾：① 批量/Promise 路径（rpc_common.c.j2）缺字符串分支，const char* 直接传给 string_ref_t 形参 —— rpc_api.fbs 一直带这个 bug；② 响应解码用 &fb_response 传指针给按值接收的访问器；③ 标量数组解码用 builder 的 flatbuffers_uint8_vec_start 读已完成的向量，flatcc 0.6 里向量指针本身即数据首地址；④ finalize_buffer 的缓冲区用 flatcc_builder_aligned_free 释放而非 free()。6 个 schema 生成代码现已全部零错误零警告"
  source: "实测：逐 schema gcc -fsyntax-only 统计 (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]

- time: 2026-09-29T08:10:47
  kind: evidence
  summary: "log_service 两个示例正式纳入构建：DSL 生成上移到根 CMake 的 generate_dsl 目标（产出 generated/log_service），dsl_codegen 测试与两个示例共用同一份产物。log_simple_demo 存在与 log_service_demo 完全相同的三处腐化（flatcc 根指针当结构体、oneway 多余空响应、连接回调旧签名）。顺带修 concurrent_demo 的格式串（4 个参数对 5 个转换符，CI 警告门禁下次会拦）"
  source: "干净 configure 后两个示例均构建并 exit 0；全量 106/106；generated/ 与 glibc 之外零警告 (2026-09-29)"
  affects: [uvrpc-tech-stack-lineage]
