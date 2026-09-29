---
id: uvbus-transport-abstraction
title: "UVBus：5 个平级传输驱动共用一份 vtable 契约"
category: decision
status: active
tags: [transport, architecture, uvbus]
created: "2026-09-28T17:09:45"
updated: "2026-09-29T09:49:58"
---

<!-- compiled_truth -->
UVBus 是 uvrpc 的传输层：一个 `uvbus_t` 总线对象 + **一份 7 槽 vtable**，5 个传输驱动平级实现它（tcp / udp / ipc / inproc / sameloop）。没有"主传输"与"备用传输"之分。

## vtable 契约（`include/uvbus.h:129-137`）

```c
typedef struct uvbus_transport_vtable {
    int  (*listen)(void* impl, const char* address);
    int  (*connect)(void* impl, const char* address);
    void (*disconnect)(void* impl);
    int  (*send)(void* impl, const uint8_t* data, size_t size);
    int  (*send_to)(void* impl, const uint8_t* data, size_t size, void* target);
    int  (*broadcast)(void* impl, const uint8_t* data, size_t size);
    void (*free)(void* impl);
} uvbus_transport_vtable_t;
```

关键约定：**每个槽的参数都是 `void* impl` 而非具体类型**。传输专有状态（`uv_tcp_t`、`uv_pipe_t`、socket、sameloop 的 server 条目）藏在 impl 结构里，总线层只持有指针。因此新增传输 = 新增一个 `.c` + 在 `create_transport()` 的 switch 里加一行，**不需要动总线、RPC 层、或任何调用点**。这是"5 个传输实现同一套语义"能成立的结构性原因。

`listen` 与 `connect` 是**同一个 vtable 的两条入口**：一个 transport 对象要么作为 server listen，要么作为 client connect，由上层 `uvbus_server_new` / `uvbus_client_new`（`include/uvbus.h:279,320`）决定，而不是靠不同的 vtable。回调（`recv_cb/connect_cb/close_cb/error_cb` + `recv_ctx/callback_ctx`）挂在 `uvbus_config` 上（`include/uvbus.h:140-158`）再拷进 `uvbus_transport`（`:159+`）——**不是** `uvbus_server_set_recv_callback()` 之类的 setter（若干旧文档写的 setter 不存在）。

## 分发路径：谁按什么选传输

两段，容易混淆，且**文档常写错**：

1. **RPC 配置层按地址前缀推断 enum**：`src/uvrpc_config.c:65-77`，`inproc://`(9) / `ipc://`(6) / `tcp://`(6) / `udp://`(6) / `sameloop://`(11)。无前缀则保留当前默认。README 说的 "selected by address prefix" 指的就是这里，**是准确的**。
2. **总线层按 enum 分发**：`src/uvbus.c:107-118` 的 `create_transport()` switch，调用 `src/uvbus.c:19-23` extern 的 `create_{tcp,ipc,inproc,udp,sameloop}_transport(type, loop)`。

即：**前缀只是配置语法糖，真正的选择键是 enum**；`uvrpc_config_set_transport()`（`src/uvrpc_config.c:82`）可以显式覆盖，此时前缀不起决定作用。

## sameloop：唯一"不走内核"的传输

`src/uvbus_transport_sameloop.c:1-13` 自述设计原则：无锁（单 loop）、经 `uv_async` 的 direct callback、**zero-copy（传指针，不拷字节）**。与 inproc 的区别：inproc 是 libuv 提供的进程内字节通道（仍有消息拷贝、有 buffer 上限），sameloop 是同 `uv_loop_t` 内 server/client 对象之间的指针交接，因此延迟最低。代价在 `src/uvbus_transport_sameloop.c:31`：`SAMELOOP_MAX_CLIENTS 64` —— 每个 server 的连接数是**固定大小数组**，不上堆扩容；超过 64 个同 loop 客户端不会得到优雅降级。文件头注释还点明用 `uv_async` 而非直接递归调用，是为了**防止 send→recv→send 栈叠加溢出**。

## 全局性：vtable 是 `static const`

每个传输文件的 vtable 是 `static const` 对象，实例创建时**单次赋值**给 transport 结构。实测（`objdump -t`，见 [[zero-threads-locks-globals]]）：5 个 vtable 全部落在 `.data.rel.ro.local`，即 PIE 下真正的只读数据。**唯一的文件作用域可变全局是 `g_custom_allocator`**（`src/uvrpc_allocator.c:49`），且被 `UVRPC_DEFAULT_ALLOCATOR == UVRPC_ALLOCATOR_CUSTOM` 编译门控。

## 已被删除的历史包袱

`ccd736a` 之前还存在一层 **pre-uvbus 传输**：`src/uv_transport.c`、`src/uv_transport_tcp.c`、`src/uv_frame.c`、`include/uv_transport.h`、`src/uvrpc_khash.h`。它的 `uv_transport.h` 声明了 `uv_transport_udp_create/ipc_create/inproc_create` 却**没有任何实现**，且只被彼此 include。`9917c7e` 起全部删除（−1164 行）。**任何文档里出现 `uvrpc_transport_t` / `uvrpc_transport_server_new` / `uvrpc_comm_type_t` / `uvrpc_transport_vtable_t` 都是指这层已死代码**，不是 UVBus。

## 与注册表的关系

inproc / sameloop 需要"按名字找到对端"，这份状态**不在传输对象里，也不在全局**，而在 per-loop registry（`loop->data`）。详见 [[loop-data-registry-over-global-hash]]。


## Timeline

- time: 2026-09-28T17:09:45
  kind: decision
  summary: "Created this page: UVBus：5 个平级传输驱动共用一份 vtable 契约"
  source: "git 1d2a944 → b149112 → af02fa9 (2026-02-14 → 02-16) + include/uvbus.h:129-137"
  affects: [uvbus-transport-abstraction]

- time: 2026-09-28T17:09:45
  kind: decision
  summary: "1d2a944 引入 message bus 路由组件，b149112 设计并实现 vtable 抽象层，af02fa9 完成抽象并接入 RPC 层"
  source: git 1d2a944 / b149112 / af02fa9
  affects: [uvbus-transport-abstraction]

- time: 2026-09-29T00:31:54
  kind: decision
  summary: "补齐 compiled_truth：7 槽 vtable 契约、enum 分发 vs 地址前缀分发、sameloop 零拷贝快路径、per-transport static const vtable"
  source: brain update-truth
  affects: [uvbus-transport-abstraction]

- time: 2026-09-29T03:57:31
  kind: note
  summary: "uvbus_config 的 timeout_ms / enable_timeout 与 uvbus_config_set_timeout() / _set_timeout_enabled() 已删除：五个传输没有一个读它们，vtable 里也从来没有超时槽。配置面收敛为 loop / transport / address + 四个回调 + 两个 ctx。tests/uvbus_test.c 的 Test 13（专测这两个 setter）一并移除"
  source: "2026-09-29 全量 grep src+include+tests 后删除"
  affects: [uvbus-transport-abstraction]

- time: 2026-09-29T09:49:58
  kind: evidence
  summary: "顺带发现既有缺陷（与本次 API 改动无关，已在基线 5c11cf3 上复现）：examples/test_sameloop 与 test_sameloop_multiclient 客户端收到 0 条消息——服务端收到了请求，响应没回到客户端。同 loop 上 sameloop_rpc_demo 与 test_sameloop_recursion 正常。这两个示例不在 ctest 里，所以一直没被发现"
  source: "git stash 到基线后重新构建并运行，同样失败 (2026-09-29)"
  affects: [uvbus-transport-abstraction]
