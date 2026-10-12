# Brain Index

_Auto-generated. Last updated 2026-10-12T07:25:21.912Z._

- [blocking-await-runs-the-loop-inline](pages/blocking-await-runs-the-loop-inline.md) — category: decision | tags: [async, event-loop, libuv, memory-safety, transport-invariance] | ## 契约
- [build-distribution-breakage](pages/build-distribution-breakage.md) — category: decision | tags: [build, ci, submodules] | ## 断点根因（2026-09-28 切片1）
- [callback-must-not-lend-borrowed-storage](pages/callback-must-not-lend-borrowed-storage.md) — category: decision | tags: [libuv, transport, udp, lifetime, api-design, identity] | ## 约束
- [embedded-handle-must-outlive-close](pages/embedded-handle-must-outlive-close.md) — category: decision | tags: [libuv, memory-safety, teardown, valgrind, transport] | ## 约束
- [loop-data-registry-over-global-hash](pages/loop-data-registry-over-global-hash.md) — category: decision | tags: [registry, transport, inproc] | INPROC 与 SAMELOOP 是"同进程内按名字找对端"的传输：server 挂一个名字，client 用同一个名字连上去。
- [minimal-rpcframe-schema](pages/minimal-rpcframe-schema.md) — category: decision | tags: [protocol, schema, flatbuffers] | UVRPC 的线格式刻意压到最小：`namespace uvrpc` 下**一个** `table RpcFrame`，四个字段，`root_type RpcFrame`。
- [pending-buffer-as-concurrency-control](pages/pending-buffer-as-concurrency-control.md) — category: decision | tags: [concurrency, backpressure, client] | 限流全部是**同步返回值**，不是等待队列。
- [promise-handle-lifetime](pages/promise-handle-lifetime.md) — category: concept | tags: [libuv, promise, memory-safety, valgrind, testing] | ## 约束
- [ring-buffer-over-uthash](pages/ring-buffer-over-uthash.md) — category: decision | tags: [performance, data-structure, client] | 客户端在途请求的回调路由用**数组直接寻址**，不用哈希表：`idx = msgid & (max_pending_callbacks - 1)`。
- [server-client-accounting-needs-identity-removal](pages/server-client-accounting-needs-identity-removal.md) — category: decision | tags: [server, transport, uvbus, api-design, resource-accounting] | ## 约束
- [sibling-transports-must-be-diffed](pages/sibling-transports-must-be-diffed.md) — category: decision | tags: [transport, uvbus, maintenance, review-process] | ## 约束
- [uv-run-once-benchmark-methodology](pages/uv-run-once-benchmark-methodology.md) — category: concept | tags: [benchmark, measurement, methodology] | ## 这些数字是什么量
- [uvbus-transport-abstraction](pages/uvbus-transport-abstraction.md) — category: decision | tags: [transport, architecture, uvbus] | UVBus 是 uvrpc 的传输层：一个 `uvbus_t` 总线对象 + **一份 7 槽 vtable**，5 个传输驱动平级实现它（tcp / udp / ipc / inproc / sameloop）。
- [uvrpc-tech-stack-lineage](pages/uvrpc-tech-stack-lineage.md) — category: decision | tags: [serialization, event-loop, lineage] | ## 谱系（每步都有 commit 证据，非回忆）
- [zero-threads-locks-globals](pages/zero-threads-locks-globals.md) — category: concept | tags: [constraint, concurrency, architecture] | ## 法则表述（准确版）
