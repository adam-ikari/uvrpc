---
id: minimal-rpcframe-schema
title: "线格式：单个 RpcFrame 表承载全部 RPC 语义"
category: decision
status: active
tags: [protocol, schema, flatbuffers]
created: "2026-09-28T17:09:45"
updated: "2026-09-28T17:11:51"
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
