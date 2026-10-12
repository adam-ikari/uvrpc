---
id: sibling-transports-must-be-diffed
title: "5 个平级传输会各自落后：修一处就去 diff 其余四处"
category: decision
status: active
tags: [transport, uvbus, maintenance, review-process]
created: "2026-10-12T07:25:21"
updated: "2026-10-12T07:41:34"
---

<!-- compiled_truth -->
## 约束

[[uvbus-transport-abstraction]] 说 5 个传输（tcp / udp / ipc / inproc / sameloop）平级实现同一份 vtable 契约、语义必须相同。但**没有任何机制强制它们相同**——没有共享的公共实现、没有编译期约束、没有跨传输的对照测试。于是本仓库反复出现的缺陷形态不是"写错了"，而是**"只在一个传输里修对了"**。

**规则：任何在某个传输里成立的正确处理，都要拿去 diff 其余四个。** 而且这个动作的价值是不对称的——**遗漏比写错更常见**，因为改动往往是从修好的那个驱动机械复制过去的。

## 已经踩过的五次（2026-10-11 ~ 10-12）

1. **`ebf1ea2` 的遗漏**：TCP 三个 accepted-client 错误路径（`uv_read_start` / realloc / `uv_accept` 失败 → 关闭并回收）都修了，IPC 只镜像了后两个。**`uv_read_start` 失败那条 IPC 一直没有**，于是一个永远读不了的对端留在服务端 `clients[]` 里被计数、且不可达。直到 `6160c5c` 才补上。
2. **`1ce8a11` vs `22ed29d`**：socket 两条（TCP / IPC）的"断开未上报"先修，in-process 两条（INPROC / SAMELOOP）当时**写进测试注释里明确排除**。同一个计数、同一个 `max_clients` 闸门，两条 in-process 一样把服务端锁死。半个月内必须回头补。
3. **客户端侧 `uv_read_start`**：TCP 有 `if (client)` 判空，IPC 直接 `&client->pipe_handle` 解引用。同一个函数形状，一个有守卫一个没有。
4. **错误返回值**：`udp_broadcast()` 与 `udp_send_to()` 检查了 `uv_write` / `uv_udp_send` 的返回并回收，TCP / IPC 的 broadcast 循环和 UDP 的注册包**没有**（`d8d749c`）。
5. **`send_to` 的目标校验**（`35694e3`，最能说明问题的一次）：`uvbus_send_to()` 把 client 参数**原样**下传，所以"目标可用"这个检查属于每个 `send_to` 槽位。四条都做了——tcp / ipc / udp 拒空目标，inproc 还额外拒 inactive 和无 callback 的客户端。**SAMELOOP 什么都不查**，直接 `__builtin_prefetch(&client->recv_cb)` 再 `client->recv_cb(...)`，调用方给个空目标就是 NULL 解引用、进程直接 segfault。修前的门禁输出里，四条打印 OK、第五条 exit 139。

## 怎么做：diff 是审计手段，不只是编辑手段

拿到一个传输上的修复后，直接把那几行拿去和其余四个驱动的对应函数逐段比对，而不是凭记忆判断"应该都差不多"。上面五次里有四次是**并排 diff 才看得出来**的，凭印象会全部漏掉。

三个信号，见到任何一个就说明值得停下来比对：

- 某个传输的函数里有 `if (client)` 之类的守卫，别处没有。
- 一个驱动检查了 `uv_*` 的返回，另一个直接丢掉。
- 修复的注释只提到某一条传输的名字。

**反过来也有用**：读到某个驱动里一段看起来"多余"的防御代码时，先去问另外四个有没有——它往往就是别人已经踩过的坑留下的形状。

## 什么时候可以不同

**不是所有差异都是缺陷**，语义确实不同的地方要保留：

- sameloop 的 `SAMELOOP_MAX_CLIENTS 64` 是固定数组（零堆分配是它的设计目标）。
- **refcount 只有 TCP / IPC 有**。UDP / INPROC / SAMLOOP 的 `*_free` 直接 `uvrpc_free(transport)`，看起来像遗漏，其实不是：TCP / IPC 的 connect 是异步的（`uv_udp_connect` 之类会留一个回调在飞），必须由 `on_client_connect` 放掉最后那个引用；另外三条的 connect 是同步完成的，没有回调会活过 connect 调用。**这一条是审出来后确认为"设计"的例子**。
- UDP 没有连接生命周期，因此**没有"离开"信号**，`client_addrs` 只增不减是无连接传输的固有限制（见 [[callback-must-not-lend-borrowed-storage]]）。
- SAMELOOP 的 `sameloop_client_t` 没有 `is_active`，因为它的 disconnect 直接释放客户端块而非置为 inactive——所以 `send_to` 只能查 `!client || !client->recv_cb`，查不了 active。**守卫能查什么，取决于该传输的状态机长什么样。**

判据：**差异能不能由该传输的设计目标推出来**。能，就是设计；不能，就是遗漏。refcount 那条就是靠"connect 是同步还是异步"推出来的。

## 一个具体的门禁缺口

`client_churn` 四条传输都跑、`send_to_target_guard` 五条都跑、`udp_client_identity` 只跑 UDP——**跨传输语义目前靠逐个补**。像"send_to 要拒空目标""断开要上报"这种共享语义，本该由一个跑遍全部传输的表格驱动测试兜住。新增跨传输语义时，**默认写成表格驱动、全部传输都跑**，别写成单条。


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
