---
id: one-dist-dir-many-configs
title: "构建配置共用 dist/：换配置前必须清掉 lib 归档"
category: decision
status: active
tags: [build, ci, sanitizer, allocator]
created: "2026-10-17T18:22:39"
updated: "2026-10-17T18:22:39"
---

<!-- compiled_truth -->
## 约束

本仓库**所有**构建目录（`bfin` / `bdbg` / `bfin_g` / `primasan` / `primub` …）共用同一份产物目录：`dist/lib/` 与 `dist/bin/`。库归档 `dist/lib/libuvrpc.a` / `libuvrpc.so.0.1.0` / `libuvrpc_full.a` 在全机器上**只有一个文件**，内容属于**最后构建的那个配置**。

而配置差异是真实且编码进归档的：

- **分配器是 configure 期选项。** `UVRPC_ALLOCATOR_DEFAULT` 默认 `mimalloc`；本机 `bfin` 是 `mimalloc`，`primasan` / `primub` 是 `system`。于是归档里 `uvrpc_allocator.c.o` 要么引用 `mi_malloc`（并由 `uvrpc` 的 PUBLIC 链接项把 `libmimalloc` 带到链接行上），要么完全不引用 —— 后者链接行里没有 mimalloc。
- **sanitizer 插桩让对象带未定义引用**（`__asan_*` / `__ubsan_*`），只有对应配置的链接行能兜住。

## 失效机制：make 的时间戳判定跨了配置

CMake/make 只拿归档和**本构建目录**的对象文件比时间。当磁盘上的归档比本配置的对象**新**时（因为**别的配置**刚构建过），make 判定归档已是最新 → **跳过重新归档** → 本配置的二进制链接到**别的配置的归档**。

两种症状，第二种更危险：

- **会失败的**：`system` 配置蹭到 mimalloc 归档 → `undefined reference to mi_malloc`；未插桩配置蹭到插桩归档 → `undefined reference to __ubsan_handle_*`。
- **不会失败的**：反过来蹭（插桩配置链接到未插桩归档，或任何"能链上"的组合）会**链接成功**。于是一轮「ASan 全绿」可能测的其实是别的构建。本轮就撞到过：sanitizer 轮次自报 188/188 全绿，事后才发现输出目录里是 mimalloc 的 Release 归档，那一轮数字作废、重跑。

还有一层放大：同一次构建里，**依赖未变**的旧目标不会重链，仍在跑上一次的二进制。所以症状看起来只是"新加的那一个目标坏了"，很容易误判成新代码的问题 —— 实际是环境。

## 规则

**构建任何配置之前，先删库归档：**

```
rm -f dist/lib/libuvrpc.a dist/lib/libuvrpc.so.0.1.0 dist/lib/libuvrpc_full.a
cmake --build <build-dir> -j$(nproc)
```

删归档会强制 make 重新归档；归档因此比二进制新，**所有依赖它的二进制会重链** —— 这一步才是关键，只重编对象不重链二进制不够。

Release（`bfin`，mimalloc）是 valgrind 与对外数字的基准构建，所以一轮 sanitizer 之后想回到基准，也必须先清再构建，否则"Release 二进制"可能根本不是 mimalloc 的。

## 证据（2026-10-17）

- 先构建 `bfin`（mimalloc），再构建 `primasan`：新目标 `tcp_capacity` 链接失败，`undefined reference to mi_malloc`，报错指向 `libuvrpc.a(uvrpc_allocator.c.o)`。
- 按规则清归档后重跑：`primasan` / `primub` 均 clean 构建，各 190/190。
- 同一批里 `refusals` 等旧目标没有重链（源未变），仍在跑旧二进制 —— 这正是"只有新目标坏了"假象的来源。

关联：[[build-distribution-breakage]] 是同一类问题的**仓库级**版本（`.gitignore` 的裸 `dist/` 把入库路径静默截断）；这里是**本机多配置**的版本。两者的共性：**产物目录的归属不是显式的**，谁最后写谁说了算。


## Timeline

- time: 2026-10-17T18:22:39
  kind: decision
  summary: "Created this page: 构建配置共用 dist/：换配置前必须清掉 lib 归档"
  source: "2026-10-17 一轮里 primasan 链接失败 → 追到共用产物目录 + mimalloc/system 配置差异"
  affects: [one-dist-dir-many-configs]

- time: 2026-10-17T18:22:39
  kind: decision
  summary: "所有构建目录共用 dist/lib 与 dist/bin，而归档编码了配置差异（分配器 / sanitizer）；make 按时间戳跳过重新归档，于是某配置会静默链接到别的配置的库。规则：构建任何配置前先删 lib 归档。"
  source: "2026-10-17 实测 undefined reference to mi_malloc + sanitizer 轮次假绿"
  affects: [one-dist-dir-many-configs]
