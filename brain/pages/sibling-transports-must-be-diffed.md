---
id: sibling-transports-must-be-diffed
title: "5 个平级传输会各自落后：修一处就去 diff 其余四处"
category: decision
status: active
tags: [transport, uvbus, maintenance, review-process]
created: "2026-10-12T07:25:21"
updated: "2026-10-12T07:56:33"
---

<!-- compiled_truth -->
## 约束

[[uvbus-transport-abstraction]] 说 5 个传输（tcp / udp / ipc / inproc / sameloop）平级实现同一份 vtable 契约、语义必须相同。但**没有任何机制强制它们相同**——没有共享的公共实现、没有编译期约束、没有跨传输的对照测试。于是本仓库反复出现的缺陷形态不是"写错了"，而是**"只在一个传输里修对了"**。

## 已经踩过的六次（2026-10-11 ~ 10-12）

1. **`ebf1ea2` 的遗漏**：TCP 三个 accepted-client 错误路径（`uv_read_start` / realloc / `uv_accept` 失败 → 关闭并回收）都修了，IPC 只镜像了后两个。**`uv_read_start` 失败那条 IPC 一直没有**，于是一个永远读不了的对端留在服务端 `clients[]` 里被计数、且不可达。直到 `6160c5c` 才补上。
2. **`1ce8a11` vs `22ed29d`**：socket 两条（TCP / IPC）的"断开未上报"先修，in-process 两条（INPROC / SAMELOOP）当时**写进测试注释里明确排除**。同一个计数、同一个 `max_clients` 闸门，两条 in-process 一样把服务端锁死。半个月内必须回头补。
3. **客户端侧 `uv_read_start`**：TCP 有 `if (client)` 判空，IPC 直接 `&client->pipe_handle` 解引用。同一个函数形状，一个有守卫一个没有。
4. **错误返回值**：`udp_broadcast()` 与 `udp_send_to()` 检查了 `uv_write` / `uv_udp_send` 的返回并回收，TCP / IPC 的 broadcast 循环和 UDP 的注册包**没有**（`d8d749c`）。
5. **`send_to` 的目标校验**（`35694e3`）：`uvbus_send_to()` 把 client 参数**原样**下传，所以"目标可用"这个检查属于每个 `send_to` 槽位。四条都做了，**SAMELOOP 什么都不查**，直接 `__builtin_prefetch(&client->recv_cb)` 再调用，空目标就是 NULL 解引用、进程 segfault。门禁输出里四条打印 OK、第五条 exit 139。
6. **payload 校验**（`306ce80`，最能说明"该在哪一层修"的一次）：`uvbus_send/send_to/broadcast` 把 payload 原样下传，TCP / UDP 构帧时 `memcpy(size 字节)` **发生在检查有没有对端之前**。无对端时两条都提前返回 `UVBUS_OK`（没看 payload 就说没问题），有对端时就是从不存在的地址读。IPC / INPROC / SAMELOOP 早就拒空 payload。

## 该在哪一层修：看不变量属于谁

第 5、6 两次看起来同构，**修法却相反**，区别在于不变量属于谁：

- **传输特有的不变量 → 补进每个驱动。** "目标可用"就是：`target` 在 4 条传输上是客户端 handle、在 UDP 上是 `sockaddr`，含义不同、判据不同（INPROC 还多一层 `is_active`），只能各自查。
- **跨传输的不变量 → 提到入口层修一次。** "payload 是能读的字节"在 5 条上传输含义完全相同（字节数 + 长度），于是**在 `uvbus.c` 三个入口各加一行** `uvbus_payload_is_usable()`，而不是在 5 个驱动 × 3 个槽位各加一遍。

这样做的收益不只是省代码：**新增传输自动继承入口层的守卫**，而 per-transport 的守卫会随新增传输重新漏掉——第 1、3、5 次全是这么漏的。

**已有的 per-transport 守卫保留**，别删：vtable 槽位在理论上不经总线层也能到达，入口层守卫是主承重、传输层守卫是纵深防御。回归矩阵验证了这个主次：只撤入口层守卫 → `payload_guard` 在第一条传输就 segfault；只撤 TCP 的传输层守卫 → 仍然全绿。

## 怎么做：diff 是审计手段，不只是编辑手段

拿到一个传输上的修复后，直接把那几行拿去和其余四个驱动的对应函数逐段比对，而不是凭记忆判断"应该都差不多"。上面六次里有五次是**并排 diff 才看得出来**的，凭印象会全部漏掉。

三个信号，见到任何一个就说明值得停下来比对：

- 某个传输的函数里有 `if (client)` 之类的守卫，别处没有。
- 一个驱动检查了 `uv_*` 的返回，另一个直接丢掉。
- 修复的注释只提到某一条传输的名字。

**反过来也有用**：读到某个驱动里一段看起来"多余"的防御代码时，先去问另外四个有没有——它往往就是别人已经踩过的坑留下的形状。

## 什么时候可以不同

**不是所有差异都是缺陷**，语义确实不同的地方要保留：

- sameloop 的 `SAMELOOP_MAX_CLIENTS 64` 是固定数组（零堆分配是它的设计目标）。
- **refcount 只有 TCP / IPC 有**。UDP / INPROC / SAMLOOP 的 `*_free` 直接 `uvrpc_free(transport)`，看起来像遗漏，其实不是：TCP / IPC 的 connect 是异步的，必须由 `on_client_connect` 放掉最后那个引用；另外三条的 connect 是同步完成的，没有回调会活过 connect 调用。**这一条是审出来后确认为"设计"的例子**。
- UDP 没有连接生命周期，因此**没有"离开"信号**，`client_addrs` 只增不减是无连接传输的固有限制（见 [[callback-must-not-lend-borrowed-storage]]）。
- SAMELOOP 的 `sameloop_client_t` 没有 `is_active`，因为它的 disconnect 直接释放客户端块而非置为 inactive——所以 `send_to` 只能查 `!client || !client->recv_cb`。**守卫能查什么，取决于该传输的状态机长什么样。**

判据：**差异能不能由该传输的设计目标推出来**。能，就是设计；不能，就是遗漏。

## 门禁现状

- `client_churn`：四条传输（UDP 无连接生命周期，跑不了"断开"）。
- `send_to_target_guard`：五条 × 空目标。
- `payload_guard`：五条 × 三个入口（`send` / `send_to` / `broadcast`）× server + client 双向。
- `udp_client_identity`：只 UDP。

跨传输语义**默认写成表格驱动、全部传输都跑**，别写成单条——第 2、5、6 次之所以能被抓住，全是因为测试把五条并排跑了一遍。


## Timeline

- time: 2026-10-12T07:25:21
  kind: decision
  summary: "Created this page: 5 个平级传输会各自落后：修一处就去 diff 其余四处"
  source: created via brain create-page
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:25:21
  kind: decision
  summary: "本仓库反复出现的缺陷形态不是'写错了'，而是'只在一个传输里修对了'。5 个驱动共用一份 vtable 契约，语义必须一致，但没有任何机制强制它们一致——所以每一次修复都要拿去 diff 其余四个。"
  source: "commits ebf1ea2, 1ce8a11, 22ed29d, 6160c5c"
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:25:21
  kind: decision
  summary: "6160c5c 补上 IPC 侧 uv_read_start：ebf1ea2 修 TCP 三条错误路径时只镜像了两条。加上 d8d749c 的三个未检查返回，本页记满四次'只修了一个传输'。"
  source: "commits 6160c5c, d8d749c"
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:41:34
  kind: decision
  summary: "本仓库反复出现的缺陷形态不是'写错了'，而是'只在一个传输里修对了'。5 个驱动共用一份 vtable 契约，语义必须一致，但没有任何机制强制它们一致——所以每一次修复都要拿去 diff 其余四个。"
  source: "commits ebf1ea2, 1ce8a11, 22ed29d, 6160c5c, 35694e3"
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:41:34
  kind: decision
  summary: "35694e3：send_to 目标校验，四条做了 SAMELOOP 没做，NULL 目标直接 segfault。第五次也是最能说明问题的一次——同一个 vtable 槽位的契约，靠人肉保持一致。"
  source: commit 35694e3
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:56:33
  kind: decision
  summary: "本仓库反复出现的缺陷形态不是'写错了'，而是'只在一个传输里修对了'。5 个驱动共用一份 vtable 契约，语义必须一致，但没有任何机制强制它们一致。修法分两种：传输特有的不变量补进每个驱动，跨传输的不变量提到入口层修一次。"
  source: "commits ebf1ea2, 1ce8a11, 22ed29d, 6160c5c, 35694e3, 306ce80"
  affects: [sibling-transports-must-be-diffed]

- time: 2026-10-12T07:56:33
  kind: decision
  summary: "306ce80：第六次漂移，也是分层结论成型的一次。目标校验属传输、payload 校验属入口层——同构的两个缺陷，正确的修法层不同。"
  source: commit 306ce80
  affects: [sibling-transports-must-be-diffed]
