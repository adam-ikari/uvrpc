---
id: nested-payload-needs-aligned-frame-start
title: "嵌套 payload 的 8 字节对齐要求帧起点也对齐"
category: decision
status: active
tags: [protocol, transport, alignment, flatbuffers]
created: "2026-11-20T23:50:04"
updated: "2026-11-20T23:50:04"
---

<!-- compiled_truth -->
## 约束

`RpcFrame.data` 是不透明的 `[ubyte]`，业务 payload 是另一个完整的 FlatCC buffer，handler 零拷贝直接读。编码侧现在把该向量按 8 字节对齐（`UVRPC_PAYLOAD_ALIGNMENT` / `create_payload_vec`），但 FlatBuffers 的对齐是相对**帧缓冲区起点**计算的。因此每个 transport 在调用 `recv_cb` 前必须保证帧起点本身是 8 字节对齐的；否则 payload 里的 int64/double 仍会经未对齐指针读取，UBSan 会报，x86 上静默。

**UDP server 是例外**：线格式前有 4 字节长度前缀，直接把 `buf->base + 4` 交给 RPC 层会让帧起点只有 4-mod-8。修法不是在解码侧补偿，而是在 UDP server 接收路径把帧 `memcpy` 到 `uvrpc_alloc` 的对齐缓冲，与 TCP/IPC/UDP client 一致；INPROC/SAMELOOP 直接传编码器输出，其 flatcc buffer 基址 ≥8 对齐，安全。

## 为什么是跨层不变量

只改 `uvrpc_encode_*` 不够：编码器只能保证 payload 相对帧起点的偏移，控制不了帧被放到哪块内存。新增 transport 或改动 framing（前缀、切片、零拷贝转发）时都必须重新核对“帧起点 8 对齐”。这与 [[sibling-transports-must-be-diffed]] 里已有的漂移同形：本次是 UDP server 的 4 字节前缀让编码侧修复失效。

与 [[callback-must-not-lend-borrowed-storage]] 合看：对齐副本同样是 transport 拥有的临时缓冲，只在 `recv_cb` 期间有效；handler 不能把 `req->params` 存起来跨回调使用（见 [[minimal-rpcframe-schema]]）。


## Timeline

- time: 2026-11-20T23:50:04
  kind: decision
  summary: "Created this page: 嵌套 payload 的 8 字节对齐要求帧起点也对齐"
  source: "commit 502413a + tests/acceptance/udp_payload_alignment.c"
  affects: [nested-payload-needs-aligned-frame-start]

- time: 2026-11-20T23:50:04
  kind: decision
  summary: "记录跨层对齐不变量：编码器保证 payload 相对帧起点 8 对齐，每个 transport 必须交付 8 对齐的帧起点；UDP server 的 4 字节前缀是例外，已复制到对齐缓冲"
  source: "commit 502413a + tests/acceptance/udp_payload_alignment.c"
  affects: [nested-payload-needs-aligned-frame-start]
