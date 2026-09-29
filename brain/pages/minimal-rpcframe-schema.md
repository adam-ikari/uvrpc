---
id: minimal-rpcframe-schema
title: "线格式：单个 RpcFrame 表承载全部 RPC 语义"
category: decision
status: active
tags: [protocol, schema, flatbuffers]
created: "2026-09-28T17:09:45"
updated: "2026-09-29T05:23:09"
---

<!-- compiled_truth -->
UVRPC 的线格式刻意压到最小：`namespace uvrpc` 下**一个** `table RpcFrame`，四个字段，`root_type RpcFrame`。

```
msgid:  uint32     // 路由键，环形缓冲数组用它取模（见 [[ring-buffer-over-uthash]]）
type:   uint8      // 0=Request, 1=Response(最后一帧), 2=ResponseMore(流式，后续还有)
method: string     // 仅 Request 使用
data:   [ubyte]    // Request 的入参 / Response 的结果
```

五个决策点：

1. **一帧通吃**：普通 RPC、Oneway、Stream（多响应）、Broadcast 全部复用同一结构，靠 `type` 与 `method` 区分。没有 error frame 专用结构体，也没有 Unknown 类型（`d2cf72c` 删掉的）。
2. **`data` 是不透明的 `[ubyte]`**：业务 payload 由 FlatCC 单独序列化后塞进 `data`。因此 handler 拿到的是原始字节指针，可以直接零拷贝读 —— 框架层不理解 payload，这正是换掉 msgpack 想要的语义（见 [[uvrpc-tech-stack-lineage]]）。代价：`method`/`data` 指针**只在 handler 回调期间有效**（`include/uvrpc.h:248` 明示），要跨回调使用必须自己拷贝。
3. **`type=1` vs `type=2` 就是 Stream 的全部协议**：服务端对同一 msgid 连发多帧 `ResponseMore`，最后一帧用 `Response`。客户端不需要 session 状态机。
4. **错误不用独立帧类型**：状态码用 `-1` 表达（`8babf1a`）。
5. **Oneway 不是线格式概念，是客户端本地决定**（实测 `src/uvrpc_client.c:634-673`）：`uvrpc_client_call_oneway` 编码出与 regular 调用**完全相同**的 `type=0` Request 帧，唯一区别是不占用环形缓冲的回调槽位。**含义**：服务端与抓包都无法区分 oneway 与 regular；服务端若对 oneway 请求回了响应，该帧会因找不到槽位而被丢弃，而不是协议错误。设计新语义时不要指望线上能表达"不要回"。

第六个决策点（2026-09-29 补）：**帧缓冲区的分配器归属属于 flatcc，不属于 uvrpc**。`uvrpc_encode_*()` 返回的字节来自 `flatcc_builder_finalize_buffer()`，而 flatcc 的 builder 用的是自己的分配器（`flatcc_builder_default_alloc`，默认即 malloc），没有接入 `uvrpc_alloc` 钩子。契约因此是：**任何 encode 输出只能用 `free()` 释放，绝不能交给 `uvrpc_free()`**。库内部统一走 `uvrpc_free_encoded()`（`src/uvrpc_flatbuffers.h` 里包一层的 `free()`，17 处调用点全走它）。这不是洁癖：system 分配器构建下两者同源、看不出差别，mimalloc 与 custom 构建下就是跨堆释放（实测 custom 下每轮往返 8 次）。

**为什么不让 flatcc 走 `uvrpc_alloc`**：flatcc 以静态库 `deps/flatcc/lib/libflatcc.a` 的形式预先编译（`scripts/setup_deps.sh`），改它的分配器只能 (a) 在 `setup_deps.sh` 里给整个 builder.c 加 `-DFLATCC_REALLOC=uvrpc_realloc`，那会让 libflatcc.a 反向依赖 libuvrpc.a，而 `uvrpc_merged` 又把两者 `ar x` 折叠进同一个 `libuvrpc_full.a` —— GNU ld 对单个归档内的循环引用不保证收敛；或 (b) 在 `src/uvrpc_flatbuffers.c` 里照抄一份 `flatcc_builder_default_alloc` 改用 `uvrpc_realloc`，40 行会随 flatcc 上游演进而失同步。两者都比"用 `free()`"贵，且会让已生成的 client.c 与仓库例子（它们都写 `free()`）集体变成错的。**含义**：自定义分配器的用户不必担心 —— 框架自己的内存走池，帧缓冲走 libc，两条路径各自闭合。

字段命名曾踩坑：`params` → `data`（`f77d728`），改的是 schema 字段名，生成的 reader/builder 符号跟着变，DSL 与手写代码要同步。


## Timeline

- time: 2026-09-28T17:09:45
  kind: decision
  summary: "Created this page: 线格式：单个 RpcFrame 表承载全部 RPC 语义"
  source: "schema/rpc.fbs + git 8babf1a / d2cf72c / f77d728 (2026-02-28)"
  affects: [minimal-rpcframe-schema]

- time: 2026-09-28T17:11:51
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [minimal-rpcframe-schema]

- time: 2026-09-29T05:22:58
  kind: decision
  summary: "帧缓冲区的分配器归属：flatcc 拥有 uvrpc_encode_*() 的输出，释放走 uvrpc_free_encoded()（即 free()），不走 uvrpc_free()"
  source: "src/uvrpc_flatbuffers.h, tests/allocator_ownership_test.c, docs/api/generated-api.md (2026-09-29)"
  affects: [minimal-rpcframe-schema, uvrpc-tech-stack-lineage]

- time: 2026-09-29T05:23:09
  kind: decision
  summary: "加入帧缓冲区的分配器归属规则：flatcc 拥有 encode 输出，释放走 uvrpc_free_encoded()"
  source: "src/uvrpc_flatbuffers.h + tests/allocator_ownership_test.c (2026-09-29)"
  affects: [minimal-rpcframe-schema]
