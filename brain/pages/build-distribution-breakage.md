---
id: build-distribution-breakage
title: "构建分发断点的根因与依赖 pin 契约"
category: decision
status: active
tags: [build, ci, submodules]
created: "2026-09-28T16:50:36"
updated: "2026-10-06T00:03:27"
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

- time: 2026-09-29T08:40:55
  kind: reversal
  summary: "反转：CI 首次全绿。修完 DSL 生成目录/cppcheck 后，run 36543459605 的 8 个 job 与 CI、Benchmark、文档部署三条流水线全部 success —— 而此前可查的每一次运行（最近 20 次，最早 2026-02-19）都是 failure。构建/分发这条线从今天起由流水线本身守护"
  source: "gh run view 36543459605：非 success 的 job 数为 0 (2026-09-29)"
  affects: [build-distribution-breakage]

- time: 2026-10-01T07:54:23
  kind: evidence
  summary: "**示例腐化的根因是「从未注册」而非漏生成**（2026-09-30）：`examples/` 有 58 个源文件，实际产出二进制的 44 个，**23 个有源码无二进制**。逐个试编译后：**13 个编译干净、只是从未注册**（scenario_1~5、simple_stream_dsl、stream_api_demo、test_10_requests、test_semaphore/2、test_simple_server、uvbus_minimal/standalone/working_test）；`scenario_4_broadcast_mode` 是 `printf` 参数列表里误插一行 `fflush(stdout);` 的语法错误；`multi_service_loop_reuse` / `test_multi_services` 是 API 漂移（生成器 create_server 加了 registry 参数、create_client 加了 connect callback）；`rpc_dsl_usage_example` 的 include 路径写成了 `../../include/`（它并不在子目录里）。README 此前称这些「多数需要额外 schema 生成」—— **不实**，13 个只要注册即可。剩下 8 个确有障碍：5 个需要把 `generate_dsl` 从硬编码 log_service 泛化到多 schema（或为 rpc_example.fbs 生成 flatcc 读码器）；`stream_dsl_demo` 用了生成器不产出的类型（`_t` vs `_ref_t`，示例与生成器契约不一致）；`generated_client_example` / `test_retry` 需要 `rpc_benchmark_builder.h` 而仓库**从无** `rpc_benchmark.fbs`。判据：注册本身就是防腐化手段 —— 今天验收套件挖出的缺陷根因都是「没有测试会编译这段代码」"
  source: "examples/*.c 逐个试编译 + dist/bin 反查（2026-09-30）"
  affects: [build-distribution-breakage]

- time: 2026-10-01T10:17:37
  kind: reversal
  summary: "**生成器不剥离注释 —— 真实缺陷，已修**（2026-09-30）。`tools/uvrpcc.py` 的 `parse()` 把四个正则（namespace / table / enum / rpc_service）全部跑在**含注释的原文**上；逐行处理时虽剥离注释（第 117 行），但 service 正则扫的是全文。于是 `schema/rpc_api.fbs` 里那段讲语法的块注释被当成真 service，生成 `rpc_servicename_*.c` 引用不存在的 `rpc_ResponseType_table_t`，**任何用到该 schema 的目标都链接失败**。修法：读入后一次性 `re.sub` 剥离块注释与行注释再解析，下游全部干净。同时 `generate_dsl` 从硬编码单个 schema 泛化为 `uvrpc_dsl_generate(prefix schema outdir ...)` 函数，并导出 `${prefix}_SOURCES` / `${prefix}_CLIENT_SOURCES` / `${prefix}_APIS` 三个命名列表 —— 变量名不能含路径里的斜杠，且 server stub 会引用本 service 的 handler，只用客户端的 demo 不该被迫实现它（`test_multi_services` 即是）。另一个真实发现：**server stub 链接即要求实现该 service 的 handler**，所以不能把两个 schema 的生成源合并进同一个变量（曾导致 `dsl_codegen_test` 与 log_service demo 被 rpc_api 源污染而链接失败）。`rpc_user_impl.c` **故意没有 `main`** —— 它是实现片段，不该注册为可执行目标。示例从 23 个无二进制降到 4 个（`generated_client_example` / `test_retry` 需要仓库从未存在的 `rpc_benchmark.fbs`；`stream_dsl_demo` 用生成器不产出的 `_t` 而非 `_ref_t`）"
  source: "gcc -c 定位到 rpc_servicename_client.c:146 undefined type（2026-09-30）"
  affects: [build-distribution-breakage]

- time: 2026-10-01T10:53:56
  kind: evidence
  summary: "**58 个示例源码，57 个产出二进制**（2026-09-30 完成）。唯一例外 `rpc_user_impl.c` **故意没有 `main`** —— 它是「填入你的服务逻辑」的实现片段，不是可运行程序，正确用法是连同生成的 `_server_stub.c` 编进自己的可执行文件（README 已给出 cc 命令）。最后三个的根因各不相同：**(1) `stream_dsl_demo.c` 把 flatbuffers 读法写成 C++ 成员访问**（`chunk->chunk_id`）并用一个两边都不存在的类型 `StreamRequest_t`；flatcc 的 `as_root` 返回表指针，字段必须走生成的访问器 `StreamChunk_chunk_id(chunk)`；另有一个 wrapper 函数定义嵌在 `main` 内部（C89 不允许嵌套函数）。**(2) `test_retry.c` / `generated_client_example.c` 对着一个早已改名的 API 世代写的**：`rpc_client_create()`（现为 config + `uvrpc_client_create`）、响应字段 `resp->data`（现为 `resp->result`）、`BenchmarkService_Add()`（不存在）、`rpc_benchmark.fbs`（仓库从未有）。**重试功能本身一直存在**，只是搬到了 client 上（`uvrpc_client_set_max_retries(client, n)`），所以这是可救的漂移而非虚构特性 —— 注意与更早的 `rpc_register_all()` / `MathService_Add()` 区分，后者确实从未实现。修法：把 `schema/benchmark.fbs` 也接入 `uvrpc_dsl_generate`，用其 `rpc_service BenchmarkService` 生成 `uvrpc_benchmarkservice_Add`。**(3) 连接回调是 `uvrpc_client_connect_with_callback()`**，`uvrpc_client_connect()` 无回调参数 —— 写示例时容易踩「看起来应该带回调」的直觉"
  source: "58/57 产出核对；flatcc 访问器与 as_root 语义查 deps/flatcc flatbuffers_common_reader.h:575（2026-09-30）"
  affects: [build-distribution-breakage]

- time: 2026-10-01T11:39:35
  kind: reversal
  summary: "**构建竞态：uvrpc_merged 覆盖 libuvrpc.a 与链接并发**（已修，2026-09-30）。`uvrpc_merged` 是 `ALL` 目标，它刻意把 `dist/lib/libuvrpc_full.a` **覆盖**到 `dist/lib/libuvrpc.a`（因为示例用 `-static` 链合并归档，需要 libuv/flatcc 内联）。但所有示例目标链的是 CMake 目标 `uvrpc`，CMake **完全不知道**这个文件路径会被重写 —— 于是 `ld` 可能正在读 `libuvrpc.a` 时被覆盖，报 `error adding symbols: no more archived files`。**今天新增约 20 个示例目标后并发链接者暴增，CI 的 ASan job 首次暴露**；本地表现为需要反复 `rm -rf dist`，我当时误判为「共享 dist 的混合状态」，实际是同一个缺陷。修法：给每个链 `uvrpc` 的目标加 `add_dependencies(<t> uvrpc_merged)`（示例 14 处显式 + tests/ 与 tests/integration/ 各用 `get_property(BUILDSYSTEM_TARGETS)` 批量加）。验证：CI 同样的 ASan 配置连做三次干净构建，0 错误。注意 `BUILDSYSTEM_TARGETS` 不含子目录目标，所以 integration 子目录要单独加"
  source: "CI 日志 'error adding symbols: no more archived files' + CMakeLists.txt:374 的 copy 指令（2026-09-30）"
  affects: [build-distribution-breakage]

- time: 2026-10-01T12:11:25
  kind: reversal
  summary: "**CI 的 warning-clean 检查抓出两类问题**（已修，2026-09-30）。**(1) 嵌套函数导致可执行栈**：`examples/udp_rpc_demo.c` 有 2 个、`tests/unit/test_boundary_values.c` 有 19 个回调定义在函数体内（GNU 扩展，trampoline 落在栈上），使目标文件带可执行 .note.GNU-stack，链接器告警 `requires executable stack` 而 CI 判定失败。修法：全部移到文件作用域；原本靠嵌套捕获的 `ctx` / `max_size` / `chunks_received` 改走回调本来就有的 `void* ctx` 参数（两个标量加进 `test_context_t`）。**踩坑**：给结构体加字段后，原有的位置化初始化会把 `&loop` 悄悄挪进新字段 —— 已全部改为零初始化加逐字段赋值。**(2) 生成的客户端 API 签名是 POJO 不是 flatcc 指针**：生成的 wrapper 签名形如 `const benchmark_AddRequest_t*`，而 `benchmark_AddRequest_t` 是生成器在 `*_rpc_common.h` 里造的 **POJO 结构体**，不是 flatcc 的表指针类型。我先前三个示例直接传了 `*_as_root()` 的表指针，触发 `-Wincompatible-pointer-types`（CI 失败）。正确用法是填 POJO 再传地址，**不需要手搓 flatbuffers**。注意该 API **非对称**：请求侧是 POJO（代你序列化），响应侧回调拿的仍是原始字节（需 `*_as_root` 解析）。**另发现一个从未运行的死测试**：`tests/unit/test_boundary_values.c` 有 main、有 9 月 23 日签入的二进制，但**没有构建目标** —— 几个月来从未编译、从未在 CI 跑。注册后发现它本身是坏的：9 个轮询循环用 `uv_run(UV_RUN_DEFAULT)`，而监听的 server 让 loop 永不返回，第一个调用就死锁（旧二进制同样卡，只是卡在更后面）。最小探针证明**库没问题**（connect/请求/响应全通）。修好轮询后 Test 1 仍断言失败（空响应未到达），**判定超出「修 CI 警告」的范围 —— 修到能跑等于重写一个 900 行套件，故撤销注册**，但保留可执行栈修复（CI 需要）与其内部改进"
  source: "CI 日志 'requires executable stack' + 三个示例的 -Wincompatible-pointer-types；最小探针 /tmp/p1 证明库正常（2026-09-30）"
  affects: [build-distribution-breakage]

- time: 2026-10-06T00:03:27
  kind: evidence
  summary: "**frame decoder fuzzer 已接入 CI 并跑 4500 万次**（2026-10-06）。攻击面是网络库的解码路径，fuzz job 用 clang + libFuzzer 直接编译（不走 CMake，因 libFuzzer 自带 main 会让 CMake 编译器测试失败），本地 160 万次/9 秒无崩溃，CI 上 4500 万次/61 秒无崩溃无 UB。**两个坑**：(1) `rpc_reader.h` 是 flatcc 构建产物**不入库**，fuzz job 绕过 CMake 时必须在编译前手动跑 `flatcc -c -v -w -o generated schema/rpc.fbs`（镜像 generate_flatcc）。(2) **YAML 重复键**：把生成步骤插进前一个 step 的 name 和 run 之间会产生重复 `run:` 键；Python 的 `yaml.safe_load` **默认保留最后一个重复键、不报错**，本地看起来有效但 GitHub 直接拒绝整个 workflow（run 在启动前即失败，报 workflow file issue）。修后必须用会抛异常的自定义 loader 检测重复键，不能信默认解析。判据：CI 的构建错误要区分『workflow 语法层』与『任务执行层』——前者任何 job 都不会跑"
  source: ".github/workflows/ci.yml fuzz job；CI 日志 Done 45029162 runs（2026-10-05）"
  affects: [build-distribution-breakage]
