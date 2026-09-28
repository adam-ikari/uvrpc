---
id: pending-buffer-as-concurrency-control
title: "并发限流：无锁三层拒绝（槽位满 / max_concurrent / 传输背压）"
category: decision
status: active
tags: [concurrency, backpressure, client]
created: "2026-09-28T17:15:24"
updated: "2026-09-28T17:15:24"
---

<!-- compiled_truth -->
<current best understanding — replace this with the real content>

## Timeline

- time: 2026-09-28T17:15:24
  kind: decision
  summary: "Created this page: 并发限流：无锁三层拒绝（槽位满 / max_concurrent / 传输背压）"
  source: "git e231041 → b06a903 (2026-02-19) + src/uvrpc_client.c:525-558, 705-730 现状核对"
  affects: [pending-buffer-as-concurrency-control]

- time: 2026-09-28T17:15:24
  kind: note
  summary: "2026-09-28 核对：三层拒绝已解耦；generation 防回绕机制从未递增，属未完成的空壳"
  source: "src/uvrpc_client.c:74,205,286,534,548 全量 grep"
  affects: [pending-buffer-as-concurrency-control]
