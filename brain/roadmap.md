---
slug: roadmap
title: Roadmap
role: milestones
updated: "2026-09-29T04:54:45"
---

# Roadmap

> ⚠️ **本页可信度说明**：仓库里**没有任何计划文档、milestone、issue 或 git tag**可以证明未来计划。下面"已完成阶段"是从 `git log`（231 commits，2026-02-09 → 2026-07-13）**反推的历史**，有证据；"待定切片"是从 root page `stack` 的 Open items 与代码/文档矛盾处**推导的候选工作**，**不是已确认的计划**，请维护者确认后再当作 roadmap 用。

## 已完成阶段（有 git 证据）

```mermaid
gantt
  title UVRPC 已走过的阶段（源自 git log，非未来计划）
  dateFormat YYYY-MM-DD
  axisFormat %Y-%m

  section 技术选型试错
  nanomsg / ZeroMQ + msgpack 起步        :done, a1, 2026-02-09, 3d
  转 NNG + msgpack                       :done, a2, after a1, 3d
  定型 libuv + FlatCC（三轮收敛）        :done, a3, after a2, 4d

  section 核心能力成型
  config builder / await / 并发组合子    :done, b1, 2026-02-12, 6d
  分配器三选一 + mimalloc 默认           :done, b2, after b1, 3d
  benchmark 与 100k ops/s 打法           :done, b3, after b2, 5d

  section 传输与抽象
  UVBus vtable 抽象层 + loop 注入         :done, c1, 2026-02-20, 5d
  UDP / IPC / INPROC 逐个打通            :done, c2, after c1, 6d
  SAMELOOP 快路径追平 INPROC             :done, c3, after c2, 6d
  Broadcast + pump timer + Oneway        :done, c4, after c3, 5d

  section 异步原语
  Promise 组合子 all/race/allSettled     :done, d1, 2026-02-25, 4d
  uvasync 调度器 + 信号量限流            :done, d2, after d1, 3d

  section 质量硬化（production iteration）
  消除 file-scope 全局 / 删锁与 atomics   :done, e1, 2026-07-01, 5d
  warning-gate + ASan + cppcheck CI      :done, e2, after e1, 4d
  修 event-loop 误用 / UAF / 传输 bug     :done, e3, after e2, 4d
  perf_benchmark 替换挂死的 benchmark     :done, e4, after e3, 3d
  VitePress 站点重建 + 文档清理          :done, e5, after e4, 3d
```

最后一个里程碑提交是 `105a666`（2026-07-13 的 production hardening 合并），证据文档在根目录 `PRODUCTION_ITERATION_2026-07-13.md`：107/107 ctest、ASan 干净、`src/` 零可变全局、warning-clean、5 传输性能数字。

## 切片状态

按"当前最挡路 → 最不挡路"排序，来源是 root page `stack` 的 Open items 与代码/文档矛盾。**1–3 与 6–7 已完成，4 半完成，5 待讨论，8–10 是本轮评审新查出的候选**。

1. ~~**修复构建/分发断点**~~ **✅ 完成并端到端验证（2026-09-28/29）**：范围比原估计更大 —— 另发现 `cmake/Dependencies.cmake` 从未入库、libuv/flatcc gitlink 指向幽灵提交两个隐藏断点。真·新克隆（干净 clone + setup_deps.sh + build.sh）在默认(mimalloc)与 system 两条路径都能构建通过。评审又翻出并修了三处：切片1自报的"零警告"被陈旧 generated/ 掩盖、README/docs 的构建链漏 `setup_deps.sh`、`uvrpc_merged` 的 `ar x` 按 basename 压平丢了 `uv_random`。随后的设计哲学评审删除了一整层 pre-uvbus 死代码（`uv_transport*.c` / `uv_frame.c` / `uvrpc_khash.h`）。记录于 [[build-distribution-breakage]]。
2. ~~**文档与代码对齐**~~ **✅ 完成（2026-09-29）**：`docs/guide/design-philosophy.md`、`docs/architecture/{index,integration}.md`、`docs/guide/benchmark.md`（整页重写）、`docs/guide/single-thread-model.md`、`docs/build-install.md`、`docs/development/{coding-standards,doxygen-examples}.md`、`docs/api/{index,generated-api}.md`、`docs/quick-start.md` 与 `docs/zh/guide/quick-start.md` 全部按代码现状改写；README 与站点/SEO 元数据的性能数字统一为 ~4.9 µs / ~205,000 req/s。`docs/api/generated-api.md` 的两段示例经**真实编译并跑通**（`Add result: 30`），顺带纠正了"把 flatcc 根指针直接当 table 用"这个会读错字段的示例错误。**结构性事实**：`docs/zh/guide/{design-philosophy,api-guide,single-thread-model}.md`、`docs/zh/build-install.md`、`docs/zh/development/{coding-standards,doxygen-examples}.md` 是指向根目录同名文件的符号链接，所以"英文"页面正文本就是中文；要真正分语言，得先把符号链接换成独立文件。
3. ~~**补齐 mimalloc 构建的 CI 覆盖**~~ **✅ 完成（2026-09-28）**：随切片 1 解决 —— CI 新增 `default-allocator` job，所有 job 先跑 `setup_deps.sh`。
4. **发布工程**（剩余部分）：`LICENSE` 已补、`libuvrpc_full.a` 丢符号缺陷已修；`Version` 徽章指向的 `v1.0.0a` release 仍不存在 —— 需打 tag 并在 GitHub 建 release，徽章才名副其实。
5. **未决的架构问题**（需讨论，工期不定）：`loop->data` 占用是"框架抢用户字段"的妥协方案，是否有更干净的挂载点（per-loop hash key，或显式 `uvrpc_registry_t*` 由用户传入）值得重新评估 —— 当前唯一"框架悄悄动了用户可见字段"的地方，见 [[loop-data-registry-over-global-hash]]。
6. ~~**环形缓冲 `generation` 机制**~~ **✅ 完成（2026-09-29）**：按"删除无用"处理 —— `generation` 从 `pending_callback_t` 与 `uvrpc_client` 双双移除，msg-match 成为唯一的槽位校验，陈旧项回收死路径随之消失。见 [[ring-buffer-over-uthash]]。
7. ~~**只写的并发/性能字段**~~ **✅ 完成（2026-09-29）**：`performance_mode` / `batching_*` / `pool_size` / `timeout_ms` / `pump_interval` / `send_pending` 连同其公开 setter、`uvrpc_perf_mode_t` 枚举、`uvbus_config` 的超时字段全部删除；`current_concurrent` 改为在单次与批次两条路径一致记账（此前只在批次递增、却在所有响应里递减，会向负漂移），`max_concurrent` 因此真正约束单次调用。新增 `UVRPCQuotaLiveTest.MaxConcurrentGatesSingleCalls` 锁住该语义。细节与保留/删除清单见 [[pending-buffer-as-concurrency-control]]。
8. **codec 缓冲区与分配器不一致**（未处理，需先定契约）：`uvrpc_encode_*()` 返回 flatcc 自己分配的缓冲区，而 `src/uvrpc_client.c` 用 `uvrpc_free()` 释放 —— mimalloc/自定义分配器构建下是跨堆释放。服务端侧（`src/uvrpc_server.c:117`）用的是 `free()`，是对的。两条修法：给 builder `flatcc_builder_set_alloc()` 挂上 `uvrpc_*`，或把客户端侧释放统一改成 `free()`。选哪条决定"分配器抽象到底覆不覆盖 flatcc"，属于契约变更。
9. **examples/README.md 仍索引不存在的示例**（未处理，小）：`complete_example.c`、`broadcast_publisher.c` / `broadcast_subscriber.c`、`broadcast_service_demo.c`、`tcp_rpc_demo.c` 等 9 处引用在 `examples/` 里没有对应文件（`perf_mode_demo.c` 一项已随性能模式 API 删除而清掉）。广播类示例是被砍掉的功能残留，要么补回示例要么删条目。
10. **benchmark 的 UDP 语义**（未处理，小）：`perf_benchmark` 顺序 ping-pong 下 UDP 丢包表现为超时而非吞吐下降，程序没有区分"丢了"和"慢了"；参考表里 UDP 与 TCP 只差几微秒，在真实网络上不成立。要么给 UDP 报告重传/丢失计数，要么在文档里把 UDP 数字标成"仅本机 loopback 有效"。

**明确不做**（依据 root page `background` 的 Non-goals）：多线程并发模型、跨语言绑定、大 payload 传输通道。
