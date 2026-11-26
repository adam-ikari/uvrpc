# UVRPC 最终状态 — 2026-11-26

本文件是本轮工作的可查汇总。每条结论给出出处（commit / CI run / 文件），证据等级分开写。

## 发布

- 最新正式发布：`v0.1.3`（release `UVRPC 0.1.3`），tag 指向 `f68e47b`。
- 历史 tag 全部保留：`v0.1.0` → `v0.1.1` → `v0.1.2` → `v0.1.3`。出处：`git tag -l`、`gh release list`。
- 版本号源：`CMakeLists.txt:2` 的 `project(uvrpc VERSION 0.1.3 ...)`；README 徽章同步指向 `v0.1.3`。
- 发布资产：`uvrpc-0.1.3-linux-x86_64.tar.gz`，sha256 `e16709ed0cc2356edb7e3db21aecf5876c5619d571320857b2d75a360bebe240`。下载件与本地一致，静态与共享链接 smoke 均通过。
- ABI：SONAME 仍为 `libuvrpc.so.0`，与 v0.1.0 起各版本兼容；仅版本化文件名随 VERSION 变化（`libuvrpc.so.0.1.3`）。

## 代码与测试

- 当前 HEAD `3bc5d69`（`test(tcp_capacity): wait on events, not fixed turns or a 2s budget`）；main 与 origin/main 同步。
- tag `v0.1.3` 落后 2 个提交：brain 发布记录 `f2ff40c` 与 `tcp_capacity` 测试修复 `3bc5d69`。这两项都不改库的线格式或 ABI，未单独发版。
- 全量测试：207/207 通过（Release mimalloc，本地 `ctest`；证据见最近的本地回归运行）。
- CI：11 个 job 全 success（run `45770171013`），含 Release/Debug × ubuntu-22.04/latest、ASan、UBSan、fuzz、cppcheck、mimalloc、文档构建。`tcp_capacity` 在所有 job 通过，约 10.6 秒。
- Benchmark 工作流 success（run `45770170989`）；文档部署 success（run `45770170998`）；线上站点 `/guide/benchmark` 返回 200。

## 本轮完成的工作（按主题）

1. **性能与内存**：到期回调扫描从每次全扫 65,536 槽改为活期限索引，单次 TCP 往返降 63%；默认 pending ring 从 `1<<16` 降到 `1<<10`，峰值 RSS 少约 508 KiB，延迟无差异。见 brain `deadline-sweep-index`、`pending-ring-size-balance`。
2. **与 gRPC 对比**：新增 C++ gRPC 同步 unary harness 与 UVRPC 阻塞 await harness；CI 上 UVRPC 约 5x 低延迟、6x 低 RSS。**对比是信息性数据，不是门禁**——外部库表现随硬件/版本/负载漂移，不该决定本项目 CI 成败。见 brain `grpc-comparison-benchmark`（含 reversal）。
3. **一致性修复**：handler 注册查重改为与查找一致的小写化（修 `echo`/`Echo` 重复注册静默接管路由）；socket 三传输的 9 处重复构帧提取到 `src/uvbus_frame.h`；15 处裸 `UVRPC_ERROR` 换成具体错误码；嵌套 payload 的最终 buffer 8 字节对齐。
4. **CI 门禁**：性能基线只由 `Benchmark` 工作流产生，`ci.yml` 不再跑重复的无门禁性能 job；传输门禁比同族比值（跨族比值测硬件、不进门禁）；新 CPU 自动记录而非失败；发布资产新增真实链接 smoke（旧的两符号 `nm` 检查不足以证明资产可用）。
5. **文档**：中英 README、首页、benchmark 指南、design-philosophy（新增"奥卡姆原则：重复的不变量只留一份"）已同步；所有性能数字带 run ID 与 host，本机数字明确为历史优化证据而非基线。
6. **测试稳定性**：`tcp_capacity` 的 filler 握手等待从固定 20000 轮改为墙钟预算+事件判据，peer 预算 2s→10s；本地 5/5、交错 3/3、全量 207/207、CI 全 job 通过。

## 已知边界与未决

- **gRPC 对比不设门禁**：这是刻意决定，不是缺口。门禁只守可归因为自身代码的量。
- **跨族比值不进门禁**：`sameloop/tcp` 在不同 CPU 上从 0.033 到 0.104，测的是硬件。整个 socket 族一起变慢时同族比值看不见，这是"测库而非测机器"的接受代价。
- **TLS / 认证 / 服务发现**：网关层职责，明确 Non-goal（root page `background`、brain `tls-auth-deferred`）。
- **多线程模型 / 跨语言绑定 / 大 payload 通道**：明确 Non-goal。
- **本机性能数字**：5800H 开发机 load 12–16 时 TCP 约 54 µs、UDP spread 2.2x；同代码 CI 上 TCP 17 µs、spread 1.0x。本机数据只作诊断，不进基线。
- **`tcp_capacity` 修复的验证等级**：结构风险消除是查证（代码 + 全量测试）；"高负载下修复有效"是推断——load 12–16 下旧版与新版都通过，原失败未能稳定复现。brain 用 decision + evidence 两条分别记录。

## 证据入口

- 发布资产与说明：`https://github.com/adam-ikari/uvrpc/releases/tag/v0.1.3`
- CI run：`45770171013`；Benchmark run：`45770170989`；文档部署：`45770170998`
- 项目 brain：`/home/gem/project/uvrpc/brain/`（29 个页面，6 个 root 页，`brain lint-links` 通过）
- 反思日志：`/home/gem/project/chat/2-Areas/reflection/`（每轮交付的三问记录）
