---
id: callback-must-not-lend-borrowed-storage
title: "回调不能交出自己不拥有的存储：向上交的指针必须比回调活得久"
category: decision
status: active
tags: [libuv, transport, udp, lifetime, api-design, identity]
created: "2026-10-12T06:46:53"
updated: "2026-11-10T03:08:03"
---

<!-- compiled_truth -->
## 约束

一个回调把指针交给上层时，**上层通常会把它存起来**：存进表、下一次比对、或者寄回去当目的地。所以那个指针的寿命必须覆盖上层的全部用法，而不是只覆盖回调返回前。

libuv 的 recv 回调尤其容易踩：`uv__udp_recvmsg()`（`deps/libuv/src/unix/udp.c:213`）里 `struct sockaddr_storage peer;` 是**栈上局部**，第 267 行把 `&peer` 交给 `recv_cb`。它在回调返回后即失效。

## 症状：同步路径永远是对的

这是本类缺陷最坑的地方。**如果上层在同一个调用链里就把事情做完，指针还活着，一切正常。**

UDP 服务端的典型链路是同步的：
`uv__udp_recvmsg` → `on_server_recv` → `server_recv_callback` → handler → `uvrpc_request_send_response` → `uvbus_send_to` → `udp_send_to`，全程仍在 `peer` 的栈帧内。于是回包**准确无误**，`test_udp` / `span_invariance` 全绿，valgrind 也全绿（栈帧虽死但内存仍映射，读到的是被复用的栈，valgrind 认为合法）。

**唯一的受害者是身份**：每个数据报、每个对端都解析到同一个栈槽，于是每个对端都和第一个记下来的比较相等。`uvrpc_server_get_client_count()` 对任意数量的 UDP 客户端**恒返回 1**，而 `max_clients` 正是拿这个数字当闸门——闸门骑在一个只能是 1 的数上。

**结论：这类缺陷只能靠针对性断言测试抓。** valgrind / ASan 结构上都看不见（libuv 预编译未插桩，ASan 的 `detect_stack_use_after_return` 也插不进去）。`udp_client_identity` 修前修后是 187 个测试里**唯一**红的一个。

## 现行做法

**不要交出临时存储，交出自己拥有的那一份。** UDP transport 本来就有一份稳定且按对端唯一的副本：`client_addrs[]`（为 broadcast 而建，正是那段扫描循环在填它）。所以把扫描得到的**槽位下标**留下来，交 `&client_addrs[peer_idx]`：

- **稳定**：堆上，和 server 同寿。
- **按对端唯一**：不同对端进不同槽。
- **仍可当地址用**：`udp_send_to` 照旧把它 cast 成 `sockaddr*`。

槽满时**交 `NULL` 而不是退回 `addr`**——退回等于把悬垂指针重新塞进上层的表里。`uvrpc_response_send()` 判到 NULL 返回 `UVRPC_ERROR_INVALID_PARAM`，干净地失败。

分配侧的保证要一起看：`client_addrs` 按 `max_clients`（UDP 固定 1024）一次 `calloc`，而写入侧的上界是同一个 `max_clients`，所以**指针永不因 realloc 失效**。改这块时这两处必须保持一致，否则就是新的悬垂。

## 与 [[server-client-accounting-needs-identity-removal]] 的关系

两页是同一件事的两半：

- 那页说**上层按身份记账，所以删除通知必须带身份**。
- 这页说**身份本身必须是能被保存的那种东西**。

顺序上先有这页——如果 `client_ctx` 交上去的是临时存储，那么那页辛苦维护的台账表里存的全是悬垂指针，压缩、比对、通知都建立在沙上。

## 证据（2026-10-12）

门禁 `tests/acceptance/udp_client_identity.c`：单 loop 上一个 UDP 服务端 + 两个独立 UDP 客户端，各打一次 `ping`。修前唯一红的是 `FAIL: a second peer is counted apart from the first`，exit 1；两条连接、两次调用、两次回包、以及 `count == 1` 那半**全部通过**——和上面"同步路径是对的"完全吻合。修后全绿。

回归：只把 `recv_cb` 的第 3 参数改回 `(void*)addr` → 187 个测试里**恰好这一个**红。

验证：187/187 ctest、ASan 183/183、UBSan 183/183、valgrind 于 `udp_client_identity` / `client_churn` / `client_teardown` 全 0。

## 遗留

UDP 的 `client_addrs` **只增不减**（`num_clients` 从不回退）：无连接的传输没有"离开"信号，服务端无从得知某个源地址不再来了。长时间运行、且对端经 NAT 端口轮换时，1024 个槽会逐渐被陈旧地址占满，之后新对端拿不到槽（本改动下表现为拿不到 `client_ctx`，回包失败）。要做对需要**空闲过期**，那是设计变更而非修补。

`tests/acceptance/span_invariance.c` 里关于 `client_ctx` 每条传输类型不同的注释已同步更新。


## Timeline

- time: 2026-10-12T06:46:53
  kind: decision
  summary: "Created this page: 回调不能交出自己不拥有的存储：向上交的指针必须比回调活得久"
  source: created via brain create-page
  affects: [callback-must-not-lend-borrowed-storage]

- time: 2026-10-12T06:48:10
  kind: decision
  summary: "回调签名里的指针，一旦被上层存起来、比较、或回寄，它的寿命就必须覆盖上层的用法。交出栈上的临时存储，症状是'看起来能用'——同步路径永远是对的，坏的只有身份。"
  source: "commit 07c71b0 + deps/libuv/src/unix/udp.c:213 + tests/acceptance/udp_client_identity.c"
  affects: [callback-must-not-lend-borrowed-storage]

- time: 2026-10-12T06:48:10
  kind: decision
  summary: "UDP 侧改用 client_addrs 槽位下标作为 client_ctx；槽满交 NULL 而非退回 addr。udp_client_identity 门禁两对端计为二。"
  source: commit 07c71b0
  affects: [callback-must-not-lend-borrowed-storage]

- time: 2026-11-09T21:09:59
  kind: evidence
  summary: "**生成的 *_sync() 交出的字符串是副本，不再是借用（2026-11-09）**。DSL 模板的 sync 回调把 string 字段直接赋成指向 resp->result 的指针，而同步路径在返回前就 free 了响应缓冲；valgrind 实测 Get 返回后读取该指针是 \"Address ... inside a block of size 36 free'd\"（释放点在 client_recv_callback）。修法：sync 回调用 uvrpc_strdup() 复制顶层 string 与 struct array 元素内的 string，并生成 uvrpc_<svc>_<Method>_response_free() 释放它们（api.h.j2 声明、client.c.j2 定义）。**未修的同族缺陷**：rpc_common.c.j2 里 Promise.all/any/race 仍按指针别名赋值，且无调用者，需要在同一轮里补复制与数组释放函数——记在 [[build-distribution-breakage]] 时间线。"
  source: "tests/generated_api_test.c + valgrind on dist/bin/generated_api_test（2026-11-09）"
  affects: [build-distribution-breakage]

- time: 2026-11-09T23:57:43
  kind: evidence
  summary: "**组合器同样适用：/ 的返回值现在拥有字符串副本（2026-11-09）**。 返回 calloc 的 struct 数组 + 每个元素内  的字符串， 逐元素调用  再 ； 只写调用方提供的 struct， 不释放结构体本身。 仅在返回 OK 时写入，调用前须零初始化。校验手段：测试里在 helper 返回后读字符串（此前即悬垂读），valgrind 0 errors。"
  source: "tests/generated_api_test.c get_all/get_any 用例"
  affects: [callback-must-not-lend-borrowed-storage]

- time: 2026-11-10T00:01:32
  kind: evidence
  summary: "**更正上一条 evidence 的文字损坏（2026-11-09）**：反引号标识符被 shell 展开吞掉，完整语义如下。组合器同样适用\"回调不得交出借用存储\"：`_all()` 返回 calloc 的 struct 数组，每个元素内是 `uvrpc_strdup()` 的字符串副本，`uvrpc_<svc>_<M>_results_free()` 逐元素调用 `..._result_free()` 再 `free(数组)`；`_any()` 只写调用方提供的 struct，`..._result_free()` 不释放结构体本身。`_any()` 仅在返回 OK 时写入，调用前须零初始化。校验：测试在 helper 返回后读字符串（修复前即悬垂读），valgrind 0 errors，ctest 200/200。"
  source: "tests/generated_api_test.c get_all/get_any（2026-11-09）"
  affects: [callback-must-not-lend-borrowed-storage]

- time: 2026-11-10T03:08:03
  kind: evidence
  summary: "`[string]` 向量现已支持，契约与单字符串一致但按元素执行：响应侧生成代码为指针数组 `calloc()`、每个元素 `uvrpc_strdup()`，`response_free` / `result_free` / `results_free` 逐元素 `uvrpc_free()` 后 `free()` 数组；空向量为 NULL/0；请求侧 NULL 元素编码为空字符串。回归测试 `tests/generated_strings_test.c`（SAMELOOP，19 项断言，valgrind 干净，ctest 201/201）。此前生成器显式拒绝该形状，模板若按标量数组 memcpy 会交出悬垂指针。同轮发现并修复解析器缺陷：字段以换行而非分号切分，`table T { a: int; b: string; }` 会静默丢掉除首字段外的全部字段。"
  source: "tools/templates/client.c.j2, tools/templates/rpc_common.c.j2, tools/uvrpcc.py"
  affects: [callback-must-not-lend-borrowed-storage]
