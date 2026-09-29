---
id: build-distribution-breakage
title: "构建分发断点的根因与依赖 pin 契约"
category: decision
status: active
tags: [build, ci, submodules]
created: "2026-09-28T16:50:36"
updated: "2026-09-29T08:33:16"
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
- **覆盖查找路径的缓存变量名是 `LIBUV_INSTALL_DIR` / `FLATCC_INSTALL_DIR` / `MIMALLOC_INSTALL_DIR`**（不是 `*_ROOT`）。文档若写错名字，读者会以为自己配好了依赖而 configure 仍在 flatcc 处失败。

## 文档站点的同类断点（2026-09-29 已修）

`docs/node_modules/` 曾有 1703 个被提交的文件，而 `.gitignore:3` 的裸 `dist/` 规则把每个 npm 包的可执行载荷排除在提交之外 —— **入库的 node_modules 只剩元数据**。实测 56 个包缺 `dist`，含 `vitepress`（缺 `dist/node/cli.js`）、`vite`、`vue`、全部 `@vue/*`、`@shikijs/*`。结果 `npm run docs:build` 在干净克隆上直接 `ERR_MODULE_NOT_FOUND`，而 `npm install` 因 lockfile 完整性校验通过会报 "up to date" 且不补齐 —— 极易被误判成环境坏了。诊断办法：对每个 `node_modules/*/package.json` 检查其 `main`/`module`/`exports` 里以 `./dist` 开头的目标是否存在。

已按"从版本控制移除"处理：`git rm -r --cached docs/node_modules`（本地文件保留），`.gitignore` 原本就有 `docs/node_modules/`（对已跟踪文件无效，现在生效）。`deploy-docs.yml` 一直用 `npm ci`，所以 CI 侧无需改动 —— 也就是说 GitHub Pages 上的站点从来不是从这个 node_modules 构建出来的。

同批删除的还有 `docs/doxygen/`（579 个提交进仓库的生成 HTML，还在描述切片 1 前就删掉的死层）。`docs/Doxyfile` 保留，按需本地生成，`.gitignore` 加了 `docs/doxygen/`。

**教训（适用于整个仓库）**：`.gitignore` 的 `dist/` 同时命中 C++ 侧产物目录与 npm 包载荷目录。凡是"入库路径里含 dist/ 的内容"都要怀疑被静默截断过；验证分发必须从干净克隆重跑，不能用工作区既有产物。


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

- time: 2026-09-29T02:25:23
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-09-29 文档审计：npm 报 up-to-date 但 vitepress CLI 缺 dist/node/cli.js；扫描 node_modules 得 56 个包 payload 缺失"
  affects: [build-distribution-breakage]

- time: 2026-09-29T04:53:35
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-09-29 决策落地：docs/node_modules 取消跟踪、docs/doxygen 删除"
  affects: [build-distribution-breakage]

- time: 2026-09-29T08:21:11
  kind: evidence
  summary: "CI 一直是红的：gh run list --workflow ci.yml 最近 20 次全部 failure，最早到 2026-02-19。今天定位两处真因：① DSL 生成目录指向源码树 generated/，产物在配置期尚不存在，add_executable 报 'Cannot find source file'，5 个 job 全挂；GENERATED 属性是目录作用域，须在使用它的子目录再设一次。② 示例目标强制 LINK_FLAGS=-static，与 -fsanitize=address 互斥（cc: cannot specify -static with -fsanitize=address），ASan/UBSan 构建无法产出；现按 CMAKE_C_FLAGS 含 -fsanitize 自动跳过静态链接"
  source: "按 ci.yml 原命令本地复现与验证：ASan 配置/构建通过，test_transport_lifetime 与 uvrpc_tests(43) 全绿 (2026-09-29)"
  affects: [build-distribution-breakage]

- time: 2026-09-29T08:33:16
  kind: evidence
  summary: "CI 修复后 8 个 job 绿了 7 个，最后一个 cppcheck 报 5 条：2 条是真的（uvrpc_server.c 把可能为 NULL 的 method 传给 HASH_FIND_STR 的键 —— 解码器允许无方法名，代码自己也写了 method ? method : \"(null)\"；以及 %s 直接传 NULL），3 条是误报（nread 声明为 ssize_t，cppcheck 建模成 intptr_t，LP64 下同型），加内联抑制注明理由。cppcheck 的抑制注释必须落在它归因的那一行（格式串所在行），放在参数行无效"
  source: "本地用 ci.yml 的 cppcheck 命令复现并清零 (2026-09-29)"
  affects: [build-distribution-breakage]
