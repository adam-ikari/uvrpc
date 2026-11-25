---
id: error-code-specificity
title: "裸 UVRPC_ERROR 一律换成语义匹配的具体错误码"
category: decision
status: active
tags: [diagnostics, api, error]
created: "2026-11-25T15:51:39"
updated: "2026-11-25T15:51:39"
---

<!-- compiled_truth -->
## 问题与判据

`src/` 里有 15 处裸 `return UVRPC_ERROR;`，调用方只能知道"出错了"，无法区分原因。而枚举里早有 `ALREADY_EXISTS` / `INVALID_STATE` / `INVALID_PARAM` / `IO` 等具体码，`uvrpc_strerror()` 也有对应文本。最明确的遗漏是 `uvrpc_server_register()` 的重复注册：`server.c:365` 同类情况已返回 `ALREADY_EXISTS`，`server.c:427` 却返回裸错误。

判据：所有 15 处换成语义匹配的具体码；现有测试不得因依赖旧值而失败（真有消费者则停手报告）；新增测试必须能拦住"退回裸错误"的负控。

## 映射

- `uvrpc_server.c` 重复注册 → `UVRPC_ERROR_ALREADY_EXISTS`
- `uvrpc_async.c` 的 `!ctx->is_running` → `UVRPC_ERROR_INVALID_STATE`
- `uvrpc_async.c` / `uvrpc_primitives.c` 的 `uv_timer_init` / `uv_async_init` 失败 → `UVRPC_ERROR_IO`（底层句柄资源失败）
- `uvrpc_flatbuffers.c` 的 `start_frame()` 失败 → `UVRPC_ERROR_IO`；`verified_root()` 失败 → `UVRPC_ERROR_INVALID_PARAM`（收到的帧不是合法 flatbuffer）
- `uvrpc_client.c` 三处编码失败 → **透传** `uvrpc_encode_request()` 的返回码，不再压平成通用错误

只改 `src/` 返回码；不改枚举、线格式、公开 API、5 传输行为。错误码数值稳定，ABI 不变。

## 验证

- 负控：把重复注册的 `ALREADY_EXISTS` 改回裸 `UVRPC_ERROR`，`failure_modes` 报 1 项 FAIL；恢复后通过。
- `failure_modes.c` 新增 `case_local_error_codes()`：重复注册、未进入 async block 的调用、NULL client、坏帧解码四种具体码断言。
- 全量 207/207 通过，无测试依赖旧返回码。


## Timeline

- time: 2026-11-25T15:51:39
  kind: decision
  summary: "Created this page: 裸 UVRPC_ERROR 一律换成语义匹配的具体错误码"
  source: "2026-11-25 src/* 15 处 + failure_modes"
  affects: [error-code-specificity]

- time: 2026-11-25T15:51:39
  kind: decision
  summary: "记录 15 处裸错误的分类映射、负控与边界"
  source: "2026-11-25 src/uvrpc_{async,client,server,primitives,flatbuffers}.c"
  affects: [error-code-specificity]

- time: 2026-11-25T15:51:39
  kind: evidence
  summary: "负控：重复注册改回裸 UVRPC_ERROR 时 failure_modes 报 FAIL；修复后 207/207"
  source: "2026-11-25 build-verify ctest + negative control"
  affects: [error-code-specificity]
