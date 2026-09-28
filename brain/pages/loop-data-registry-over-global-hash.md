---
id: loop-data-registry-over-global-hash
title: "INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
category: decision
status: active
tags: [registry, transport, inproc]
created: "2026-09-28T17:08:39"
updated: "2026-09-28T17:08:39"
---

<!-- compiled_truth -->
<current best understanding — replace this with the real content>

## Timeline

- time: 2026-09-28T17:08:39
  kind: decision
  summary: "Created this page: INPROC/SAMELOOP 端点注册表：per-loop(loop->data) 取代全局 hash + rwlock"
  source: "git 4fe8b67 → 7c72b6d + src/uvbus_loop_registry.h 头注释"
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:39
  kind: decision
  summary: "4fe8b67 (2026-02-26) 移除全局变量、改静态注册表；后演进为 per-loop loop->data 方案"
  source: git 4fe8b67
  affects: [loop-data-registry-over-global-hash]

- time: 2026-09-28T17:08:39
  kind: note
  summary: "docs/guide/design-philosophy.md 仍写着'不占用 loop->data'，与实现矛盾，待切片 2 修正"
  source: "brain:stack Open item 7"
  affects: [loop-data-registry-over-global-hash]
