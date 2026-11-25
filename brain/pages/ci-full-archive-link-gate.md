---
id: ci-full-archive-link-gate
title: "发布资产要有链接门禁：只查两个符号不够"
category: decision
status: active
tags: [ci, release, quality]
created: "2026-11-25T16:36:14"
updated: "2026-11-25T16:36:14"
---

<!-- compiled_truth -->
## 盲区与判据

`libuvrpc_full.a` 是发布主资产，但仓库里没有 target 真正链接它：DSL demo 只 `add_dependencies(uvrpc_merged)`（构建顺序），链接的是 `uvrpc`。`default-allocator` job 原来只用 `nm` 查 `uv_random` 与 `_mi_random_init` 两个符号。判据：构造一个缺第三个对象（`uvrpc_strerror.c.o`）的 archive，门禁必须失败；否则该检查不能证明发布资产可用。

负控结果：缺 `uvrpc_strerror` 的 archive **通过**了两符号检查（两个目标符号都在），但 `cc ... broken.a` 报 `undefined reference to uvrpc_strerror`。因此旧门禁不构成链接验证。

## 修法

`default-allocator` job 的 `Verify merged archive` 步骤保留两个 `nm` 检查（定位历史 drop 仍有用），追加一个真实链接+运行步骤：编译一个调用 `uvrpc_strerror()` 与 `uvrpc_allocator_get_name()` 的小程序，只链接 `libuvrpc_full.a`，不接任何 `-luv/-lflatcc/-lmimalloc`，再运行校验输出。CI 路径需要 `-Iinclude -Ideps/libuv/include -Ideps/flatcc/include` —— 第一次漏了 libuv include，CI 红，补上后绿。头文件依赖外部 `uv.h` 是发布契约的一部分（用户自己提供 libuv），不是缺陷。

本地用同一段 CI 脚本验证：好 archive 打印 `full archive links, runs and is self-contained`，坏 archive 在链接处失败并被门禁拦住；CI run 45576662472 全绿且步骤实际执行。

## 核对过的其余 job（未改）

fuzz 上次 run 实际执行 47,177,625 次 / 61 秒，不是空转；ASan/UBSan 跑全部测试；debug-logging 宏是编译期 `fprintf` 开关，没有独立运行时分支，只编译不跑不会漏逻辑；`ci.yml` 的 performance job 只打印数字，但 `benchmark.yml` 已有真门禁。这些没有同等级的负控证据，不动。

关联 [[build-distribution-breakage]]、[[one-dist-dir-many-configs]]。


## Timeline

- time: 2026-11-25T16:36:14
  kind: decision
  summary: "Created this page: 发布资产要有链接门禁：只查两个符号不够"
  source: 2026-11-25 ci.yml negative control
  affects: [ci-full-archive-link-gate]

- time: 2026-11-25T16:36:14
  kind: decision
  summary: "记录 full archive 门禁盲区、负控与修法"
  source: "2026-11-25 ci.yml + archnoke 负控"
  affects: [ci-full-archive-link-gate]

- time: 2026-11-25T16:36:14
  kind: evidence
  summary: "负控：删 uvrpc_strerror.c.o 的 archive 通过两符号 nm 检查但链接失败；改为链接+运行后好/坏都正确判定"
  source: "2026-11-25 /tmp/opencode/archnoke + CI run 45576662472"
  affects: [ci-full-archive-link-gate]
