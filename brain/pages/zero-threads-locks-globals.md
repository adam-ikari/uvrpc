---
id: zero-threads-locks-globals
title: "零线程、零锁、零可变全局：贯穿全项目的第一约束"
category: concept
status: active
tags: [constraint, concurrency, architecture]
created: "2026-09-28T17:08:01"
updated: "2026-09-28T17:08:01"
---

<!-- compiled_truth -->
<current best understanding — replace this with the real content>

## Timeline

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "Created this page: 零线程、零锁、零可变全局：贯穿全项目的第一约束"
  source: "git 证据链 7c72b6d / ecff4de / 4fe8b67 + CLAUDE.md 与 PRODUCTION_ITERATION_2026-07-13.md"
  affects: [zero-threads-locks-globals]

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "7c72b6d (2026-07-24) 消除库内最后一批 file-scope 可变全局；分配器分发改为编译期宏"
  source: git 7c72b6d
  affects: [zero-threads-locks-globals, loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:01
  kind: decision
  summary: "ecff4de (2026-06-07) primitives/uvasync 删除全局 mutex、atomics、volatile 与 usleep 轮询"
  source: git ecff4de
  affects: [zero-threads-locks-globals]
