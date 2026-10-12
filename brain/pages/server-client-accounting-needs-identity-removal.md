---
id: server-client-accounting-needs-identity-removal
title: "服务端连接台账必须能按身份删除，否则泄漏会变成策略旁路"
category: decision
status: active
tags: [server, transport, uvbus, api-design, resource-accounting]
created: "2026-10-12T05:58:25"
updated: "2026-10-12T05:59:55"
---

<!-- compiled_truth -->
## 约束

服务端给每个 accepted connection 建一条记录（`uvrpc_server.client_ctxs[]` + `current_clients`），并且**删除路径必须和插入路径同时存在**。只增不减的台账有两层伤害，第二层比第一层严重得多：

1. **泄漏**：记录指向的块没人释放（TCP 块 256KB，读缓冲在块内）。
2. **策略旁路**：`max_clients` 闸门（`uvrpc_server.c:78`）比的就是这个数字。`current_clients` 于是从"计数"退化成"历史最高水位"，服务端在 `max_clients = N` 下实际只服务**前 N 个曾经连过的客户端**，之后一律返回 `UVRPC_ERROR_MAX_CLIENTS`，哪怕此刻一个连接都没有。

第 2 层有个更隐蔽的变体：台账按**指针**扫描（`server_recv_callback` 的 `is_new_client` 判定），块一旦被 free，`malloc` 就可能把同一地址分给新连接的记录——于是扫描**命中新条目**，请求被放行，闸门被静默绕过。既没有报错也没有崩溃，只是闸门时灵时不灵。

## 根因：通知带不动身份

`uvbus_close_callback_t` 是 `void (*)(void*)`，一个裸 `void*` 说不出"是**哪一个**连接走了"。而服务端正是按身份记账的——`client_ctxs[]` 存的就是那个指针。所以这个公开 typedef **结构上无法**承载删除所需的信息，不是调用方不想接。

**结论：当一个既有回调的签名装不下上层所需的信息时，加一条内部通道，不要去改那个公开签名。** 本仓库的做法是 `src/uvbus_internal.h` 提供 `uvbus_set_conn_close_handler()`，`struct uvbus_transport` 加两个私有字段（`conn_close_cb` / `conn_close_ctx`，紧挨 `callback_ctx` 这个既有锚点放）。公开头文件只做加法，`uvbus_close_callback_t` 原样不动——它仍被 `tests/uvbus_test.c:219` 和 `docs/architecture/index.md` 引用。

## 删除动作放哪：在 read 回调里，不在 close 回调里

`tcp_forget_client()` / `ipc_forget_client()` 从 `on_client_read` 的 EOF 分支调用，而**不是**挂到 `on_client_close` 上。理由和 [[embedded-handle-must-outlive-close]] 同源但方向相反：**`on_client_read` 是唯一能证明 transport 还活着的时刻**——它正握着 `transport`（经 `stream->data`）。等到 close 回调真正执行时，`tcp_free` / `ipc_free` 可能已经把 transport 释放了，此时再通过 `parent_transport` 反向找服务端就是 UAF。

顺序：**先通知（`conn_close_cb`）、再压缩表、最后 `parent_transport = NULL` + `uv_close()`**。反过来做会让回调拿到一个已经从表里消失的指针，找不到可删的条目。

## 两个必须的守卫

- **`transport->is_server`**：`on_client_read` 同时服务客户端自己连出去的那条连接（TCP `uvbus_transport_tcp.c:451`、IPC `on_client_read`）。客户端侧那个块归 `uvrpc_client` 所有、由它关闭释放，在这里 `uv_close()` 等于把指针递到已经交还的内存前面。TCP 和 IPC 两处都要判。
- **压缩而非留洞**：`clients[]` 的 broadcast / shutdown 循环按下标直接索引，TCP 的 plain broadcast 路径更是**无任何守卫**地解引用 `clients[i]`。留洞 = 下一次广播就踩空。`client_ctxs[]` 同理，且它的 `is_new_client` 扫描把洞读成"陌生指针"，会让重连的老客户端看起来是新客户端。

## 证据（2026-10-12）

门禁 `tests/acceptance/client_churn.c`（→ `client_churn` + `client_churn_memcheck`）：TCP / IPC 各 3 轮 connect/call/disconnect。**第一轮无论修没修都通过**——这正是要点，它只在"有客户端来过又走了"之后才变红。修前 TCP + IPC 共 6 次 `FAIL: the count returns to zero once the client has gone`，exit 1。

回归矩阵（链条两环各自承重，各拆一次都变红，各 6 次 FAIL）：
- 拆服务端：注释掉 `uvbus_set_conn_close_handler()` → 台账永不收缩。
- 拆传输层：`if (transport->conn_close_cb)` 改 `if (0)`（保留 close 与释放）→ 通知不发，同样 6 次 FAIL。

验证：186/186 ctest（184 + `client_churn` + `client_churn_memcheck`）、ASan 182/182、UBSan 182/182、valgrind 于 `client_churn` / `client_teardown` / `listen_failure` 全 0。ASan 与 UBSan 在修前修后**都全绿**——这类缺陷（计数语义错误）与内存安全无关，只有针对性的断言测试能抓。

## 遗留

**INPROC / SAMELOOP 两条传输不接 `conn_close_cb`**，它们的连接消失时服务端台账同样只增不减。`tests/acceptance/client_churn.c` 的注释里写明了排除。`inproc_remove_client()` 与 `client->server` 反向指针都已存在，接上是同一形状的改动。

`uvrpc_server_stop()` 只翻一个 flag，不主动断开任何连接。


## Timeline

- time: 2026-10-12T05:58:25
  kind: decision
  summary: "Created this page: 服务端连接台账必须能按身份删除，否则泄漏会变成策略旁路"
  source: created via brain create-page
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-12T05:59:55
  kind: decision
  summary: "服务端 client 台账只增不减：泄漏是表象，真正伤害是 max_clients 闸门骑在同一个数字上，于是缓慢把服务端锁死。"
  source: "commit 1ce8a11 + tests/acceptance/client_churn.c"
  affects: [server-client-accounting-needs-identity-removal]
