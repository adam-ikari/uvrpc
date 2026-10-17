---
id: server-client-accounting-needs-identity-removal
title: "服务端连接台账必须能按身份删除，否则泄漏会变成策略旁路"
category: decision
status: active
tags: [server, transport, uvbus, api-design, resource-accounting]
created: "2026-10-12T05:58:25"
updated: "2026-10-17T18:25:11"
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

**这条通道有两个订阅者。** 本页讲的是"客户端走了 → 告诉服务端"；反方向"服务端走了 → 告诉客户端"同样挂在它上面，见 [[a-client-must-be-told-its-connection-died]]。当时只建了通道和一路订阅，另一半是 2026-10-17 补的（commit b1e0ca9）。

## 上报点：socket 走 read 回调，in-process 走 disconnect

**上报必须发生在 owner 被证明还活着的那一刻。** 四条传输分成两组，各有一个这样的时刻：

- **TCP / IPC**：在 `on_client_read` 的 EOF 分支调用 `tcp_forget_client()` / `ipc_forget_client()`，而**不是**挂到 `on_client_close` 上。理由和 [[embedded-handle-must-outlive-close]] 同源但方向相反：`on_client_read` 是唯一能证明 transport 还活着的时刻——它正握着 `transport`（经 `stream->data`）。等到 close 回调真正执行时，`tcp_free` / `ipc_free` 可能已经把 transport 释放了，此时再通过 `parent_transport` 反向找服务端就是 UAF。
- **INPROC / SAMELOOP**：没有 socket 也没有 EOF，客户端靠自己的 `disconnect` 离开。两条传输**本来就**调了 `inproc_remove_client()` / SAMELOOP 表压缩，自己的记账是对的——缺的只有上报那一步。上报点就在删表之前，从 `endpoint->server_transport` / `server->server_transport` 反查服务端 transport。

两组的可达性论证是同一个，且都建立在**已经存在的守卫条件**上：`client->server_endpoint` / `client->server` 非 NULL，当且仅当服务端条目还活着——服务端自己的 disconnect 会在释放前把每个客户端的反向指针置空。SAMELOOP 原先没有这条反向指针，`sameloop_server_t` 补了 `server_transport` 字段（`sameloop_listen` 里赋值）；INPROC 的 `inproc_endpoint_t.server_transport` 本来就有。

顺序一律是：**先通知（`conn_close_cb`）、再压缩表、最后释放 / `uv_close()`**。反过来做会让回调拿到一个已经从表里消失的指针，找不到可删的条目。

## 两个必须的守卫

- **`transport->is_server`**：`on_client_read` 同时服务客户端自己连出去的那条连接（TCP `uvbus_transport_tcp.c:451`、IPC `on_client_read`）。客户端侧那个块归 `uvrpc_client` 所有、由它关闭释放，在这里 `uv_close()` 等于把指针递到已经交还的内存前面。TCP 和 IPC 两处都要判。in-process 两条天然不会触发（`disconnect` 的 client 分支里另走一条路）。

  2026-10-17 补正：这个守卫**只该罩住 teardown，不该罩住上报**。`tcp_forget_client()` / `ipc_forget_client()` 的通知触发在各自守卫**之前**，本来就是两侧都能安全调用的；原先客户端侧整段被守卫挡住，于是"服务端走了"这一半从没上报过。修法是**把上报移出守卫**（teardown 仍留在里面），而不是在守卫外再抄一份上报。
- **压缩而非留洞**：`clients[]` 的 broadcast / shutdown 循环按下标直接索引，TCP 的 plain broadcast 路径更是**无任何守卫**地解引用 `clients[i]`。留洞 = 下一次广播就踩空。`client_ctxs[]` 同理，且它的 `is_new_client` 扫描把洞读成"陌生指针"，会让重连的老客户端看起来是新客户端。

## 两个数字：传输的连接上限 ≠ max_clients

本页的 `current_clients` **只数说过话的客户端**：`is_new_client` 扫描在**读到一条请求**时才建条目，没发过任何字节的连接不进这个数。

每个传输**另有**一层连接上限，独立、固定，`uvrpc_config_set_max_clients()` 改不动它：

| 传输 | 连接上限 | 到顶时的行为 |
|---|---|---|
| TCP | `UVBUS_MAX_CLIENTS` = 1024 | 新连接 **accept 后立刻 close**（给对端 FIN），见 d200429 |
| UDP | 1024（硬编码） | 新 peer 无槽位，回复无处可去 |
| SAMELOOP | `SAMELOOP_MAX_CLIENTS` = 64 | `connect` 同步失败 |
| IPC / INPROC | 无（表按需倍增） | — |

于是**两个数字会分开**：可以有一千条连接挂在 TCP 上、而 `uvrpc_server_get_client_count()` 报 0。两条后果都容易踩：

1. **`max_clients` 代替不了传输上限。** 调小不会让传输少收连接（连接在读到请求之前不算客户端），调大也不会让传输多收（1024 就是 1024）。两道门在不同层次，谁也夹不住谁。
2. **传输层的拒绝必须给对端一个信号**，否则连接落在 accept 队列里"连着一个空"，`max_clients` 那层根本轮不到。对 RPC 客户端而言这个信号还要有人接 —— 见 [[a-client-must-be-told-its-connection-died]]。

已落在 `include/uvrpc.h`：`max_clients` 字段与 `uvrpc_config_set_max_clients()` 的文档写明两层关系与各传输上限，并顺带修正了 `uvrpc_server_get_client_count()` 的措辞（它数的是"说过话的客户端"，不是 socket）。

## 证据（2026-10-12）

门禁 `tests/acceptance/client_churn.c`（→ `client_churn` + `client_churn_memcheck`）：**四条传输各 3 轮** connect/call/disconnect，`max_clients = 1`。**第一轮无论修没修都通过**——这正是要点，它只在"有客户端来过又走了"之后才变红。

回归矩阵（各环节独立承重，拆掉某一条只让对应传输变红，各 6 次 FAIL）：
- 拆服务端注册（`uvbus_set_conn_close_handler`）→ 四条全红。
- 拆 socket 侧上报（`if (transport->conn_close_cb)` → `if (0)`，保留 close 与释放）→ TCP + IPC 红。
- 拆 in-process 侧上报 → 只有 INPROC + SAMELOOP 红，TCP/IPC 仍绿。

验证：186/186 ctest（184 + `client_churn` + `client_churn_memcheck`）、ASan 182/182、UBSan 182/182、valgrind 于 `client_churn`（四条传输全过）/ `client_teardown` / `listen_failure` 全 0。ASan 与 UBSan 在修前修后**都全绿**——这类缺陷（计数语义错误）与内存安全无关，只有针对性的断言测试能抓。

传输层的一半另有门禁：`tests/acceptance/tcp_capacity.c`（→ `tcp_capacity` + `tcp_capacity_memcheck`）。它用 `UVBUS_MAX_CLIENTS` 条**只连不说话**的原始连接把 TCP 表填满 —— 传输数得到、RPC 层完全看不见，正是"两个数字分开"的现场 —— 然后转掉一个裸 TCP 对端和一个 uvrpc 客户端，要求两者都被告知。修前两条检查全红、耗时 5.1s（两个预算都烧完），修后 2.6s。

## 遗留

- **传输连接上限与 `max_clients` 不合并、不夹紧。** 曾考虑把 5 个 per-transport 上限收成一个共享内部常量、并在 `uvrpc_server_create` 里做 `max_clients = min(请求值, 上限)`。**只做文档、不做代码**：夹紧改的是另一道门（见上面「两个数字」），既替代不了传输层的 accept-and-close，也表达不了 UDP 与 SAMELOOP 各自不同的"到顶行为"。
- `uvrpc_server_stop()` 只翻 flag 一项已于 2026-10-17 解决：stop 关的是会话准入，不是监听器，见 [[server-stop-is-admission-not-unbind]]。注意原先那句「不主动断开任何连接」把方向写反了 —— 契约要求已有连接**照常完成**，缺的是「不接受新连接」。


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

- time: 2026-10-12T06:17:30
  kind: decision
  summary: "服务端 client 台账只增不减：泄漏是表象，真正伤害是 max_clients 闸门骑在同一个数字上，于是缓慢把服务端锁死。四条传输全部需要上报，socket 与 in-process 走的是两条不同的路。"
  source: "commits 1ce8a11 + 22ed29d + tests/acceptance/client_churn.c"
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-12T06:17:30
  kind: decision
  summary: "22ed29d 把 in-process 两条接上：SAMELOOP 补 server_transport 反向指针，INPROC 复用已有的 server_transport；上报点在各自 disconnect 的删表之前。churn 门禁从 2 条扩到 4 条传输，拆掉 in-process 上报只让 INPROC/SAMELOOP 变红。"
  source: commit 22ed29d
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-15T03:50:09
  kind: note
  summary: "max_clients 有两套天花板，且配置那一套是开口的（未修缺口）：uvrpc_config_set_max_clients(N) 没有上界钳制（uvrpc_server.c:295 只把 0 归成 1024），而传输层的上限是编译期常量、五条各不相同——TCP 1024（UVBUS_MAX_CLIENTS）、UDP 1024（udp.c:442 硬编码）、SAMELOOP 64、IPC 与 INPROC 无上限（表自动扩容，配 5000 真的能用）。uvbus_listen(bus) 不带参数，没有通道把配置传下去。于是配到 1024 以上时 TCP/UDP 超售：TCP 的 on_server_connection 在 :332 触发 error_cb 后直接 return 而不 uv_accept，内核层面握手已完成，客户端的 connect_cb 报 OK，请求却永远没人读——静默挂死；UDP 则因 peer 表满而交 NULL 身份，server_recv_callback 会把 NULL 记进 client_ctxs[]，此后所有满表 peer 被并成同一个客户端（同一类身份塌陷，只是走的是 NULL 路径而非悬垂指针路径）。因此不能一刀切把配置钳到 1024——那会让今天确实能用的 IPC/INPROC 倒退；正确修法要按传输取天花板，属于设计变更，与本页已修的台账删除缺陷分开记。"
  source: "connect_refund 与 5 驱动 vtable 对照审计（2026-10-15）"
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-17T17:17:48
  kind: decision
  summary: "stop 的遗留清空：准入层已生效，指向新页 server-stop-is-admission-not-unbind"
  source: commit 73fd203
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-17T18:23:39
  kind: decision
  summary: "补上「两个数字」一节：current_clients 只数说过话的客户端，每个传输另有一层固定连接上限，两者是不同层次的门，max_clients 既改不动也替代不了传输层。遗留改为显式记下「不合并/不夹紧」的决定。"
  source: "2026-10-17 commit d200429 + tests/acceptance/tcp_capacity.c"
  affects: [server-client-accounting-needs-identity-removal]

- time: 2026-10-17T18:23:39
  kind: evidence
  summary: "两个订阅者：客户端侧的「服务端走了→告诉客户端」是同一通道的另一半（b1e0ca9）。TCP 传输层 1024 上限到顶改为 accept-and-close（d200429），门禁 tcp_capacity 用 1024 条只连不说话的连接演示两个数字分开。结论：max_clients 与传输上限不合并、不夹紧，只写文档。"
  source: "commit b1e0ca9 + d200429, tests/acceptance/client_sees_close.c + tcp_capacity.c"
  affects: [a-client-must-be-told-its-connection-died, one-dist-dir-many-configs]

- time: 2026-10-17T18:25:11
  kind: note
  summary: "UDP 的同类隐患，换个外形：UDP 的 client 身份是 server->client_addrs[] 里的一个稳定槽位地址。若给 UDP 加空闲槽回收/LRU（\"满了就顶掉最久不说话的\"），新 peer 会继承那个槽位地址，而 is_new_client 的指针扫描会把它读成同一个客户端——不是漏掉一个客户端，而是把两个不同 peer 认成一个，回复发错人。所以 UDP 加回收前必须先给身份加上\"这一代\"的区分（或换非地址身份）。本轮只记录，不实现。"
  source: "2026-10-17 讨论：UDP 无连接，槽位复用会伪造身份"
  affects: [server-client-accounting-needs-identity-removal]
