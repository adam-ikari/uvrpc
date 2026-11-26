---
id: case-insensitive-handler-registration
title: "方法名大小写不敏感必须贯穿注册查重与查找"
category: decision
status: active
tags: [server, routing, correctness]
created: "2026-11-26T09:22:27"
updated: "2026-11-26T09:22:27"
---

<!-- compiled_truth -->
## 缺陷与判据

方法名大小写不敏感这条契约，`server_recv_callback` 在查找时守住了（`uvrpc_server.c:201-215` 把入站名小写化后 `HASH_FIND_STR`），但 `uvrpc_server_register` 的查重用的是原始大小写（旧 `:425`），存储用小写（旧 `:445`）。判据：先注册 `echo` 再注册 `Echo` 必须返回 `UVRPC_ERROR_ALREADY_EXISTS`；否则第二次静默加入另一个比较相等的 key，路由被后注册的 handler 接管。

探针实测（修前）：`reg lowercase 'echo' first: 0` / `reg uppercase 'Echo' second: 0` —— 两次都成功。这是库自身正确性缺陷，与微服务/框架无关。

## 修法

`src/uvrpc_server.c` 把小写化提前，查重和插入共用同一份 `method_lower`；`HASH_FIND_STR(server->handlers, method_lower, entry)`。存储契约不变（仍是小写名）。任何分配失败都先释放 `method_lower`。

## 验证

- 探针（修后）：0 / -13（UVRPC_ERROR_ALREADY_EXISTS=-13）
- `failure_modes.c` 的 `case_local_error_codes` 加三条断言：精确重复、先小写后大写、先大写后小写。
- 负控：把查重改回原始 `method` → 两条 case-variant 断言 FAIL；恢复后 207/207 ctest 全绿。

## 边界

只对服务端 method 注册生效。per-call timeout、重试语义、服务发现属框架/网关层 Non-goal（见 [[tls-auth-deferred]] 与 root `background`），本轮不动。


## Timeline

- time: 2026-11-26T09:22:27
  kind: decision
  summary: "Created this page: 方法名大小写不敏感必须贯穿注册查重与查找"
  source: "2026-11-26 探针 + failure_modes 负控"
  affects: [case-insensitive-handler-registration]

- time: 2026-11-26T09:22:27
  kind: decision
  summary: "记录 handler 重复注册的大小写漏洞、探针与修法"
  source: "2026-11-26 src/uvrpc_server.c + tests/acceptance/failure_modes.c"
  affects: [case-insensitive-handler-registration]

- time: 2026-11-26T09:22:27
  kind: evidence
  summary: "负控：查重改回原始大小写时两条大小写变体断言失败；探针修后 0/-13，全量 207/207"
  source: "2026-11-26 failure_modes + register_probe"
  affects: [case-insensitive-handler-registration]
