---
id: nested-payload-needs-aligned-frame-start
title: "嵌套 payload 的 8 字节对齐要求帧起点也对齐"
category: decision
status: active
tags: [protocol, transport, alignment, flatbuffers]
created: "2026-11-20T23:50:04"
updated: "2026-11-25T09:39:08"
---

<!-- compiled_truth -->
## 约束（两层都要）

`RpcFrame.data` 是不透明的 `[ubyte]`，业务 payload 是另一个完整的 FlatCC buffer，handler 零拷贝直接读。payload 里的 int64/double 经类型化指针读取，所以 payload 向量数据在**最终 buffer** 里必须 8 对齐。这要两层同时成立：

1. `create_payload_vec` 用 `flatcc_builder_create_vector(align=8)` 把向量数据相对 **emission cursor** 对齐到 8 —— 这是 2026-11-20 commit 502413a 做的一半。
2. **root 的 block_align 也必须是 8**。生成的 `RpcFrame_start_as_root()` 走 `buffer_start(B,fid)` → `flatcc_builder_start_buffer(B,fid,0,0)`，block_align=0。于是 finalize 时 4 字节 root offset 把已经在 cursor 上 8 对齐的向量平移到最终 buffer 的 **offset 36（4-mod-8）**，与 payload 大小和向量 align 取值（8/16 都试过）无关。handler 读 `scores[2]` 里的 double 就是未对齐加载，UBSan 报 `flatcc_endian.h:80 load of misaligned address`；x86 静默。

修法：`src/uvrpc_flatbuffers.c` 新增 `start_frame()`，用 `flatcc_builder_start_buffer(builder, 0, UVRPC_PAYLOAD_ALIGNMENT, 0)` + `uvrpc_RpcFrame_start()` 取代三处 `start_as_root()`（request / response / response-more）。实测 payload 偏移 36 → 40（8-mod-8）。

## 传输侧不变量仍然成立

每个 transport 在 `recv_cb` 前仍必须交付 8 对齐的帧起点。UDP server 的 4 字节长度前缀例外已由复制到对齐缓冲解决（commit 502413a），见 [[uvbus-transport-abstraction]]。本次是编码侧的第二半，两层缺一不可：只修向量 align 不够，只修传输对齐也不够。

## 负控与测试

- `generated_shapes` 在 UBSan 下原本失败（96 次 runtime error 级别 1 处，1/197 失败），修复后 0 runtime error、206/206。
- 新增 `tests/acceptance/frame_payload_alignment.c`：三种编码器 × 40 种 payload 长度，断言 params 8 对齐且 double 值完整；负控（block_align 改回 0）报 40 处 FAIL。全量 207/207。
- 与 [[sibling-transports-must-be-diffed]] 同形：三处编码器必须同时改，遗漏任何一处就留一个未对齐路径。


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

- time: 2026-11-25T09:39:07
  kind: decision
  summary: "补上第二半：root block_align=0 让 4 字节 root offset 把 payload 平移成 4-mod-8；三处编码器统一 start_frame(block_align=8)"
  source: "2026-11-24 generated_shapes UBSan + frame_payload_alignment"
  affects: [nested-payload-needs-aligned-frame-start]

- time: 2026-11-25T09:39:08
  kind: reversal
  summary: "反转'对齐已修复'：原修复只对齐向量数据，漏了 root block_align=0；4 字节 root offset 使 payload 落在 4-mod-8"
  source: "2026-11-24 generated_shapes UBSan + probe params_off=36"
  affects: [nested-payload-needs-aligned-frame-start]

- time: 2026-11-25T09:39:08
  kind: evidence
  summary: "start_frame(block_align=8) 后 params_off 36→40；generated_shapes UBSan 0 runtime error，新增 frame_payload_alignment 负控 40 FAIL，206→207 全绿"
  source: "2026-11-24 build-ubsan + build-grpc ctest"
  affects: [nested-payload-needs-aligned-frame-start]
