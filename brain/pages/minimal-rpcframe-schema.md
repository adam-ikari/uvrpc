---
id: minimal-rpcframe-schema
title: "线格式：单个 RpcFrame 表承载全部 RPC 语义"
category: decision
status: active
tags: [protocol, schema, flatbuffers]
created: "2026-09-28T17:09:45"
updated: "2026-10-01T11:29:08"
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

- time: 2026-09-30T13:33:19
  kind: evidence
  summary: "**远程可触发的越界读（已修）**：三处解码只检查 size>=8 就调用生成的 as_root()，而 flatcc 生成的读取器**不做任何边界检查** —— 缓冲区只要'看起来像'根表，后续就按对端选定的偏移解引用。其中 uvrpc_get_frame_type 最弱（只检查 size<1）且在**每个响应的客户端路径上**都跑，所以恶意/损坏的服务端同样能触发。修法用 flatcc 自带校验器（-v 生成 rpc_verifier.h，三处统一走 verified_root）。校验器在 libflatccrt.a 里，此前项目没链接；现在链接并一并折进 libuvrpc_full.a（合并脚本本来就收多个归档，不影响分发设计）。证据：校验器打桩后 tests/acceptance/protocol_robustness.c 退出码 139（段错误），正是修复前的行为"
  source: "裸 TCP 探针 + gdb（uvrpc_decode_request <- server_recv_callback <- on_client_read）；六类畸形载荷（2026-09-30）"
  affects: [minimal-rpcframe-schema]

- time: 2026-10-01T04:10:20
  kind: evidence
  summary: "**负载上限：限制作用在编码后的帧而非载荷，且编码开销不是常数**（实测：0 字节载荷→帧 38、1 字节→51、100 字节→48，属 FlatBuffers 对齐），所以'最大载荷 = 65536 - 48'只是近似。三种传输的实测边界**各不相同且可复现**：TCP 65488、IPC 65484、UDP 65452（同一次 run 内二进制搜索，确定性；连跑两次结果完全一致）。三者执行的是同一个 UVBUS_DEFAULT_MAX_FRAME_SIZE，落点不同只因各自停止尺寸处的编码开销不同；UDP 少得最多，符合它是数据报协议、有低于帧上限的自身天花板。**实际后果：跨传输统一的安全载荷上限是三者中最低的 65452，不是 TCP 的 65488** —— 按 TCP 边界选尺寸的应用，换到 UDP 或 IPC 上会静默收不到响应。另外 UVBUS_MAX_FRAME_SIZE（1MB）定义了但全库无引用，是'看起来像上限的死常量'；文档若称 1MB 为硬上限即不成立"
  source: "tests/acceptance/payload_bounds.c；三传输独立二分探针，两次复跑一致（2026-09-30）"
  affects: [minimal-rpcframe-schema]

- time: 2026-10-01T05:58:31
  kind: reversal
  summary: "**调错方法名会返回成功**（已修，2026-09-30）。服务端把失败编码进**响应帧的 payload**（裸魔数 int32 2 + 消息文本），客户端 `resp.error_code = 0` 是无条件的、从不解析该 payload —— 于是调不存在的方法得到 `status=0, error_code=0, result_size=20`，「结果」是 `\x02\x00\x00\x00Method not found` 的 ASCII 原文。**看起来完全不像错误**：任何检查 status 再解析 result 的代码会把 4 字节整数 + 15 字节文本当类型化响应读。空方法名同样。根因：错误帧与结果帧是同一个帧，而 payload 无法自证 —— handler 完全可以合法返回 int32+文本。修法：新增 `UVRPC_FRAME_TYPE_RESPONSE_ERROR = 3`（此前 type 只有 0/1/2 且全是魔数无常量），服务端走 `uvrpc_encode_error()`，客户端在读 payload **之前**先按 frame_type 判别，错误帧解出 error_code/error_message、置 status、result_size 归零。同时修 `uvrpc_response_send_error()`（公开 API，同一缺陷）。**附带修一个会静默拖垮客户端的坑**：槽位只在 `frame_type == 1` 时释放，错误帧（3）不释放 —— 回调跑了但名额不归还，配额逐渐耗尽直至拒绝所有调用。错误是完整答案，必须与 type=1 一同释放。服务端魔数 2 改为 `UVRPC_ERROR_NOT_FOUND` (-12)。**线格式变更**：新服务端配旧客户端仍表现为「成功携带错误字节」，反之旧服务端配新客户端错误帧被忽略（静默）；0.x 无 tag 无 release，此刻改最便宜"
  source: "tests/acceptance/failure_modes.c；裸探针实测 status=0/error_code=0/result_size=20（修复前）；非空转：错误帧退回普通帧 → 5 项失败，槽位不释放 → 3 项失败（2026-09-30）"
  affects: [minimal-rpcframe-schema]

- time: 2026-10-01T11:29:08
  kind: evidence
  summary: "**架构文档的行号引用大面积漂移 —— 已修**（2026-09-30）。写脚本扫 docs/ 下所有 `src/*.c:N` / `include/*.h:N` 引用，逐条核对是否指向真实代码，**16 条可疑、14 条确认漂移**。最严重的一类是指向无关内容：`include/uvrpc.h:182` 实际是注释结束符（被引用为 `uvrpc_handler_t` 的 typedef 位置，真实在 :200）；`include/uvrpc.h:465` 实际是 `@param server`（被引用为 `uvrpc_response_send`，真实在 :528）；`include/uvrpc.h:505` 实际是注释（被引用为 `UVRPC_ERROR_NOT_CONNECTED`，真实在 :54）；`src/uvrpc_client.c:146` 实际是空行（被引用为 msgid 取模，真实在 :250）。**我改客户端错误帧处理时新增了约 145 行，把大批引用顶走了**。修正后剩余 2 条经人工确认是有意引用注释开头。**判据**：行号引用是架构文档的证据链，指向空行或注释结束符时等于没有证据 —— 这与「文档承诺不存在的东西」是同一类问题，值得定期用脚本核对而非逐个人读。另修 `docs/api/generated-api.md` 两处过时示例：`create_server` 少了 registry 参数、`create_client` 少一个参数（真实 5 个：loop/address/registry/callback/ctx）。**注意区分**：`flatcc_builder_finalize_buffer` 配 `flatcc_builder_free` 与配 `flatcc_builder_aligned_free` **两种都对**（后者是 stable free），代码库 86/63 混用，模板用后者，文档用前者 —— 不是 bug，未改"
  source: "脚本核对 docs/ 全部行号引用 + 逐条语义比对（2026-09-30）"
  affects: [minimal-rpcframe-schema]
