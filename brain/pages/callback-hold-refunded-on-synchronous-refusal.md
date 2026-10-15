---
id: callback-hold-refunded-on-synchronous-refusal
title: "为回调预留的引用，必须在同步失败路径上退还"
category: decision
status: active
tags: [lifetime, refcount, async, client, uvbus]
created: "2026-10-15T03:49:12"
updated: "2026-10-15T03:49:32"
---

<!-- compiled_truth -->
## 约束

任何"**先取一个引用，等回调来还**"的模式，都必须保证两条退出路径**各还一次、且只还一次**：

1. 回调到达 → 由回调还；
2. 调用**同步**被拒绝，回调根本不会来 → 由拒绝路径还。

漏掉第 2 条，引用就永久多 1，释放函数减到 1 就停，对象永不销毁。而且这**不表现为崩溃**：块只是"再也没人交回来"，有句柄的还会让 loop 关不掉。

## 为什么同步路径最容易漏

因为成功路径是异步的：取引用 → 调 connect → 返回 OK → 回调稍后跑。写代码的人脑子里的时序是"回调会来"，注释也这么写。**同步失败打破了这个前提，但它长得和异步失败一模一样**（同一个 `if (err != OK) return`），review 时很容易看过去。

本仓库的实例（`src/uvrpc_client.c`）：

- `uvrpc_client_connect()` 先 `client->ref_count++`，注释明说"hold for the pending callback"；
- `client_connect_callback()` 里 `--ref_count == 0 → client_destroy()` 是**唯一**的归还点；
- 但 `uvbus_connect()` 同步拒绝时，两个函数都直接 `return UVRPC_ERROR_TRANSPORT`，**不还**；
- `uvrpc_client_free()` 的 `--ref_count` 只对应 `uvrpc_client_create()` 那一份，所以停在 1。

后果链：client 连同 `uvbus_t`、address、scheduler 一起不归还；scheduler 的 handle 还挂在 loop 上，`uv_loop_close()` 返回非 0。

## 可达性：五条传输里"最慢"的那条反而给了门

SAMELOOP 的 server 表固定 64 项，第 65 个 connect 在 `uvbus_connect()` **内部同步**返回 `UVBUS_ERROR_MAX_CLIENTS`，回调没跑。于是**同一个调用有两种结局，差别只在"回调是否恰好到达"**——这正是会计必须对称的理由，而不是"成功路径测过了就行"。TCP / IPC / UDP 的 connect 是异步的，永远走不到这条分支。

## 现行做法

把"还引用"收敛成一个函数（`client_drop_connect_hold()`），由**回调和两条拒绝路径共同调用**。"恰好一次"于是成为一处事实，而不是三处要保持同步的三行代码。哪条路径该还，由 `uvbus_connect()` 的返回值判定——拒绝了就不会有回调，两者永不同时发生。

## 门禁（2026-10-15）

`tests/acceptance/connect_refund.c`，**两种入口各跑一轮**（`uvrpc_client_connect` 与 `uvrpc_client_connect_with_callback` 是两条独立代码路径，各需各的证据）。

- 普通入口：泄漏 client 的 scheduler handle 让 `uv_loop_close()` 返回非 0 —— **不依赖 valgrind 也能红**。
- memcheck：525,144 bytes definitely lost。
- 回归矩阵：只回退 `connect_with_callback` 那处 → 第 1 轮绿、第 2 轮红，**每处修复独立承重**。
- 硬编码 64 是故意的：表一旦变大，第 65 个连接开始成功，该轮就大声失败，而不是测试悄悄睡着。

## 同族

[[callback-must-not-lend-borrowed-storage]] 管"交出去的指针要活多久"，这页管"取走的引用谁来还"——都是回调参与的会计，但方向相反。


## Timeline

- time: 2026-10-15T03:49:12
  kind: decision
  summary: "Created this page: 为回调预留的引用，必须在同步失败路径上退还"
  source: created via brain create-page
  affects: [callback-hold-refunded-on-synchronous-refusal]

- time: 2026-10-15T03:49:32
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [callback-hold-refunded-on-synchronous-refusal]
