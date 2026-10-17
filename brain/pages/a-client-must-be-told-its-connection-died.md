---
id: a-client-must-be-told-its-connection-died
title: "对端没了必须告诉客户端：is_connected 不能比连接活得久"
category: decision
status: active
tags: [client, transport, uvbus, lifecycle, error-reporting]
created: "2026-10-17T18:22:59"
updated: "2026-10-17T18:22:59"
---

<!-- compiled_truth -->
## 约束

`uvrpc_client.is_connected` 是客户端三个发送入口的闸门（`uvrpc_client.c:632/774/845`）。**这个缓存不能比它描述的那条连接活得久。** 服务端消失之后（被 `free`、崩溃、重启、任何原因断开），客户端必须被告知，否则下一次调用会：被接受 → 真的写出去（对端 FIN 之后**第一次**写仍然成功，这正是它不像错误的原因）→ 然后无人应答 → 调用方一直等到自己的 deadline，等的是一个不存在的服务端。

这不是"容量上限"的特例，而是**对端消失的常规路径**：任何崩溃、重启、掉线都走这里。客户端在别处（任何其他 RPC 栈）都是这样发现对端没了的。

## 一条通道，两个订阅者

上报**复用** [[server-client-accounting-needs-identity-removal]] 为服务端记账建的那条内部通道 `uvbus_set_conn_close_handler()`（`src/uvbus_internal.h`），不新造通道。理由：`tcp_forget_client()` / `ipc_forget_client()` 的通知触发在各自的 `is_server` 守卫**之前**，本来就是两侧都能安全调用的；in-process 两条的 disconnect 分支同理。结构上缺的只有两件事：

1. **客户端侧从来没订阅过** —— `uvrpc_client_create` 里补 `uvbus_set_conn_close_handler(client->uvbus, client_conn_closed, client)`，回调只清 `is_connected`。
2. **四条传输里「服务端走了 → 告诉客户端」这一半从来没上报过** —— 以往唯一的报向是反的：「客户端走了 → 告诉服务端」。

## 四条传输的两半

| 传输 | 客户端走了 → 告诉服务端 | 服务端走了 → 告诉客户端 |
|---|---|---|
| TCP / IPC | `on_client_read` 的 EOF 分支调 `tcp_forget_client` / `ipc_forget_client`（本来就有） | 同一个 `forget_client`：把**上报**从 `is_server` 守卫里移出来（守卫只留 teardown） |
| INPROC | 客户端自己的 `disconnect` 里上报（本来就有） | 服务端 disconnect 的分离循环里，**先上报再脱钩**（本轮补） |
| SAMELOOP | 同上 | 同上（本轮补；`sameloop_client_t` 新增 `client_transport` 反向指针） |
| UDP | 无连接 | **无**：没有 close 可报，客户端只能超时 —— UDP 的固有属性，不是缺陷 |

INPROC 的 `inproc_client_t.client_transport` **本来就有、赋值了却从未被读过** —— 就是为这条反向可达性留的，本轮终于用上；SAMELOOP 缺这个字段，照 INPROC 补。

## 顺序与安全

- 上报必须发生在**能证明客户端还活着**的时刻：就是 transport 自己的 read 回调 / teardown 内部。`uvrpc_client` 一定比它的 transport 活得久 —— `client_destroy()` 里 `uvbus_free()`（释放 transport）在 `uvrpc_free(client)` 之前（`uvrpc_client.c:589-616`）。所以从 read 回调里经 `conn_close_ctx` 反查客户端是安全的。
- socket 两条**只**把**上报**移出守卫。teardown（`parent_transport = NULL` + `uv_close()`）仍然只在服务端侧做：客户端侧那个块归 `uvrpc_client`，在这里 `uv_close()` 等于把指针递到已交还的内存前面。

## 故意不做

- **不在回调里拆客户端**：块归 `uvrpc_client`，由它自己关闭释放。
- **不动 `bus->is_active`**：三个入口读的是 `is_connected`，只动那个错的事实。
- **`inproc_send()` 无 endpoint 时静默返回 `UVBUS_OK`** 是另一个独立缺陷（发出去的东西没人收，却报成功）。通知到位后，经公开 API 已不可达，所以本轮不修；要修得配一个 uvbus 层的测试。

## 证据（2026-10-17，commit b1e0ca9）

门禁 `tests/acceptance/client_sees_close.c`（→ `client_sees_close` + `client_sees_close_memcheck`），**四行**：TCP / IPC / INPROC / SAMELOOP。服务端 `free` 之后，下一次调用必须**立刻**返回 `UVRPC_ERROR_NOT_CONNECTED`，而不是被接受后等超时。修前四行全红（"accepted and left to time out"）。

回归矩阵（两半各自承重，缺一不可）：只撤客户端注册 → 四行全红；只撤传输侧上报 → 四行全红；恢复 → 全绿。验证：195/195 ctest、ASan 188/188、UBSan 188/188、valgrind 于新门禁 0 errors / 0 definitely lost。

## 遗留

- **UDP 无 close 可报**（固有，非缺陷）。
- **TCP 容量上限那一半靠这条通知才能被调用方看见**：传输侧 accept-and-close 只给 FIN，没人订阅就等于没发生。见 [[server-client-accounting-needs-identity-removal]] 的「两个数字」一节与 commit d200429。


## Timeline

- time: 2026-10-17T18:22:59
  kind: decision
  summary: "Created this page: 对端没了必须告诉客户端：is_connected 不能比连接活得久"
  source: "2026-10-17 修 b1e0ca9 时确立：一条通知通道、两个订阅者，四条传输都要走"
  affects: [a-client-must-be-told-its-connection-died]

- time: 2026-10-17T18:22:59
  kind: decision
  summary: "uvrpc_client 缓存的 is_connected 是三个发送入口的闸门，不能比连接活得久。服务端消失（free/崩溃/重启/断线）后客户端必须被告知，否则下一次调用被接受、写出去（对端 FIN 后首写会成功）、无人应答，调用方等满整个 deadline。上报复用服务端记账那条内部通道，四条传输都要覆盖。"
  source: "2026-10-17 commit b1e0ca9 + tests/acceptance/client_sees_close.c"
  affects: [a-client-must-be-told-its-connection-died]
