---
id: build-distribution-breakage
title: "构建分发断点的根因与依赖 pin 契约"
category: decision
status: active
tags: [build, ci, submodules]
created: "2026-09-28T16:50:36"
updated: "2026-09-28T18:40:21"
---

<!-- compiled_truth -->
## 断点根因（2026-09-28 切片1）

任何人（含 CI）克隆 uvrpc 后都无法按 README 构建，四条独立断点：

1. **`cmake/Dependencies.cmake` 从未入库** —— `CMakeLists.txt` 强制 `include()` 它，但 `.gitignore` 的裸 `*.cmake` 规则把它静默排除，`git log --all -- cmake/` 为空。CI 曾报"107/107 通过"，其实依赖作者机器上的未跟踪文件。修复同时需加 `!cmake/*.cmake` 例外，否则补了文件仍不被跟踪。
2. **libuv/flatcc 的 gitlink 指向幽灵提交**（`d1f647d` 记录、上游从未 push），`git submodule update` 报 `not our ref`。重新钉到真实 tag：flatcc `v0.6.1`、libuv `v1.47.0`（选 1.47 而非 1.53，因 examples 依赖 `uv_loop_init` 旧语义）。
3. **`deps/mimalloc` 的 gitlink 在 `d1f647d` 被误删**（`.gitmodules` 仍声明），而默认分配器是 mimalloc → 默认构建必挂。重新加回并钉 `v2.2.7-37`（`8ff03b63`，从 `main` 可达）。
4. `build.sh`/`scripts/setup_deps.sh`/`tools/GENERATOR_QUICK.md`/`LICENSE` 被文档引用却不存在；`pyproject.toml` 的 `uvrpc-gen` entry 指向不存在的模块。

## 依赖 pin 与构建契约（durable）

- **flatcc 必须 `-fPIC` 且安装到 `deps/flatcc/{bin,lib,include}`** —— `uvrpc_shared` 链它；非 PIC 会在 `uvrpc.so` 链接期报 `R_X86_64_PC32 ... recompile with -fPIC`。`find_program(FLATCC_COMPILER)` 优先 `deps/flatcc/bin`。
- **mimalloc 只在 `UVRPC_ALLOCATOR_DEFAULT=mimalloc` 时查找**，vendored-only（历史契约如此）。
- **子模块必须 `--force`**：被中断的操作会留下只有 `.git` 的空树，`git submodule update` 会跳过它们（HEAD 已匹配却无文件）——`deps/gtest`、`deps/uthash` 就是这样，需 `--force` 或 `git checkout -f`。
- **本地 `git clone` 会复制 HEAD，不复制暂存内容**；验证未提交改动要用 `git diff HEAD --binary` 叠加到干净 `git worktree`，不能直接克隆。


## Timeline

- time: 2026-09-28T16:50:36
  kind: decision
  summary: "Created this page: 构建分发断点的根因与依赖 pin 契约"
  source: "2026-09-28 切片1调查（git 考古 + 实测 configure）"
  affects: [build-distribution-breakage]

- time: 2026-09-28T18:40:04
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [build-distribution-breakage]

- time: 2026-09-28T18:40:21
  kind: reversal
  summary: "切片1自报的'零警告'在真新克隆上不成立：generated/ 被 gitignore，本机留的是旧 flatcc 的陈旧生成码，掩盖了两处警告。教训——验证分发必须从干净克隆重跑 flatcc，不能用工作区既有产物。"
  source: "2026-09-28 评审：fresh-clone 构建日志跑 CI gate 命令 => exit 1"
  affects: [build-distribution-breakage]

- time: 2026-09-28T18:40:21
  kind: decision
  summary: "uvrpc_merged 用 ar x 把所有归档解到同一目录再 ar rcs *.o，按 basename 压平——libuv 的 src/random.c 与 mimalloc 的 src/random.c 同名相撞，合并归档静默丢一个（链 -l:libuvrpc_full.a 报 undefined reference to uv_random）。改用 ar 的 MRI 脚本(CREATE/ADDLIB/SAVE)：不落地解包、无同名冲突；并在 uvrpc POST_BUILD 先快照 pristine 归档，否则每次重跑会把上一版 merged 再 ADDLIB 进去、无限增长。"
  source: "2026-09-28 评审：merged 成员 94≠95、nm 显示 uv_random 缺失；修复后 95/95 且幂等"
  affects: [build-distribution-breakage]
