---
id: server-stop-is-admission-not-unbind
title: "uvrpc_server_stop() 关的是会话准入，不是监听器"
category: decision
status: active
tags: [server, transport, api-design, protocol]
created: "2026-10-17T17:17:14"
updated: "2026-10-17T17:17:35"
---

<!-- compiled_truth -->
## 约束

`uvrpc_server_stop()` 的文档契约是 **"Stops accepting new connections but allows existing connections to complete."** 实现只写 `server->is_running = 0`，而 `is_running` 全文只有一个读取点——`uvrpc_server_start()` 开头那个让二次 start 变 no-op 的早退。服务路径从不看它，于是"停止"过的服务端照样监听、照样 accept、照样应答；那个 flag 唯一可观察的效果，是让下一次 start 再去 listen 一次（内核以 EADDRINUSE 挡回）。2026-10-17 修（`73fd203`），门禁 `tests/acceptance/refusals.c`。

## 准入层拒绝，不是关监听器

契约的两半落在两层：

- **"不接受新连接" = RPC 会话准入**：`server_recv_callback` 里 `is_new_client && !is_running` 走与"服务端已满"**同一个**拒绝分支，同一个门，换一个码（`UVRPC_ERROR_INVALID_STATE` vs `UVRPC_ERROR_MAX_CLIENTS`）和一句话。**用拒绝分支而不是直接丢弃**：丢弃会让客户端挂在一个永远不会答的服务端上；拒绝是一个它收得到的答案。这也要求拒绝帧必须是错误帧（见 [[minimal-rpcframe-schema]] 决策点 4），否则客户端读到的是 `UVRPC_OK`。
- **"已有连接照常完成"不需要新的检查**：台账按**身份**匹配（`is_new_client` 扫 `client_ctxs[]`，见 [[server-client-accounting-needs-identity-removal]]），已在表里的客户端 `is_new_client` 恒为 false，自然绕过闸门。

## 为什么不去关监听器

"stop accepting" 的字面实现是 `uv_close()` 掉 listen handle（新 connect 立刻被 RST）。**放弃了**：

1. **vtable 没有"只关监听、不断客户端"这一档**。现有槽位只有 `listen`（开）与 `disconnect`（全拆）。要加就得改公开的 `uvbus_transport_vtable_t` 并落到五个驱动；而 TCP `disconnect` 与 listen close 回调之间共享 `server->clients` 的释放路径，提前关监听会让 IPC 那三处缺 `uv_is_closing()` 守卫的 `uv_close()` 立刻变成承重代码。
2. **契约没要求解绑端口**，它说的是"不接受新连接"。停掉准入即可，且不动已连接者。端口仍由 `uvrpc_server_free()` 释放。

## 为什么不能顺手调 uvbus_stop()

`uvbus_stop()` 只做 `bus->is_active = 0`，而 `uvbus_send` / `send_to` / `broadcast` 都以此判定"已关闭"。把 stop 路由过去，服务端就**回不了应答**——恰好违反同一句话的后半截。所以 `uvbus_stop()` 一字未动。


## Timeline

- time: 2026-10-17T17:17:14
  kind: decision
  summary: "Created this page: uvrpc_server_stop() 关的是会话准入，不是监听器"
  source: "commit 73fd203 + tests/acceptance/refusals.c"
  affects: [server-stop-is-admission-not-unbind]

- time: 2026-10-17T17:17:35
  kind: decision
  summary: "stop 的语义定在准入层：拒绝分支而非丢弃，不关监听器，也不走 uvbus_stop"
  source: "commit 73fd203 + tests/acceptance/refusals.c"
  affects: [server-stop-is-admission-not-unbind]
