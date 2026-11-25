# Brain Index

_Auto-generated. Last updated 2026-11-25T06:27:53.915Z._

- [a-client-must-be-told-its-connection-died](pages/a-client-must-be-told-its-connection-died.md) — category: decision | tags: [client, transport, uvbus, lifecycle, error-reporting] | ## 约束
- [blocking-await-runs-the-loop-inline](pages/blocking-await-runs-the-loop-inline.md) — category: decision | tags: [async, event-loop, libuv, memory-safety, transport-invariance] | ## 契约
- [build-distribution-breakage](pages/build-distribution-breakage.md) — category: decision | tags: [build, ci, submodules] | ## 断点根因（2026-09-28 切片1）
- [callback-hold-refunded-on-synchronous-refusal](pages/callback-hold-refunded-on-synchronous-refusal.md) — category: decision | tags: [lifetime, refcount, async, client, uvbus] | ## 约束
- [callback-must-not-lend-borrowed-storage](pages/callback-must-not-lend-borrowed-storage.md) — category: decision | tags: [libuv, transport, udp, lifetime, api-design, identity] | ## 约束
- [codegen-shape-matrix](pages/codegen-shape-matrix.md) — category: decision | tags: [codegen, dsl, flatcc, testing] | 生成模板是按形状分支展开的（标量数组 / 字符串向量 / 表数组 × 请求 / 响应 × _sync / _all / _any / 批量），任何一支没有 schema 实例化，它就只是"写出来但从未编译过"的文本。
- [deadline-sweep-index](pages/deadline-sweep-index.md) — category: decision | tags: [client, timeout, performance] | ## 问题与判据
- [embedded-handle-must-outlive-close](pages/embedded-handle-must-outlive-close.md) — category: decision | tags: [libuv, memory-safety, teardown, valgrind, transport] | ## 约束
- [grpc-comparison-benchmark](pages/grpc-comparison-benchmark.md) — category: decision | tags: [benchmark, grpc, methodology] | 本页结论与口径已随 2026-11-25 的 v0.1.1 发布固定：tag `v0.1.1` 指向 `1b98fd0`，资产 `uvrpc-0.1.1-linux-x86_64.tar.gz` 的 sha256 为 `90f9c44357d2814c368ea9381f3de1
- [loop-data-registry-over-global-hash](pages/loop-data-registry-over-global-hash.md) — category: decision | tags: [registry, transport, inproc] | INPROC 与 SAMELOOP 是"同进程内按名字找对端"的传输。
- [minimal-rpcframe-schema](pages/minimal-rpcframe-schema.md) — category: decision | tags: [protocol, schema, flatbuffers] | UVRPC 的线格式刻意压到最小：`namespace uvrpc` 下**一个** `table RpcFrame`，四个字段，`root_type RpcFrame`。
- [nested-payload-needs-aligned-frame-start](pages/nested-payload-needs-aligned-frame-start.md) — category: decision | tags: [protocol, transport, alignment, flatbuffers] | ## 约束（两层都要）
- [one-dist-dir-many-configs](pages/one-dist-dir-many-configs.md) — category: decision | tags: [build, ci, sanitizer, allocator] | ## 约束
- [oneway-is-declared-not-inferred](pages/oneway-is-declared-not-inferred.md) — category: decision | tags: [codegen, dsl, oneway, api] | oneway 的判据从"响应表名叫 EmptyResponse"改成方法声明上的 `(oneway)` 属性。
- [pending-buffer-as-concurrency-control](pages/pending-buffer-as-concurrency-control.md) — category: decision | tags: [concurrency, backpressure, client] | 限流全部是**同步返回值**，不是等待队列。
- [pending-ring-size-balance](pages/pending-ring-size-balance.md) — category: decision | tags: [memory, performance, ci] | ## 平衡判据
- [promise-handle-lifetime](pages/promise-handle-lifetime.md) — category: concept | tags: [libuv, promise, memory-safety, valgrind, testing] | ## 约束
- [ring-buffer-over-uthash](pages/ring-buffer-over-uthash.md) — category: decision | tags: [performance, data-structure, client] | 客户端在途请求的回调路由用**数组直接寻址**，不用哈希表：`idx = msgid & (max_pending_callbacks - 1)`。
- [server-client-accounting-needs-identity-removal](pages/server-client-accounting-needs-identity-removal.md) — category: decision | tags: [server, transport, uvbus, api-design, resource-accounting] | ## 约束
- [server-stop-is-admission-not-unbind](pages/server-stop-is-admission-not-unbind.md) — category: decision | tags: [server, transport, api-design, protocol] | ## 约束
- [sibling-transports-must-be-diffed](pages/sibling-transports-must-be-diffed.md) — category: decision | tags: [transport, uvbus, maintenance, review-process] | ## 约束
- [tls-auth-deferred](pages/tls-auth-deferred.md) — category: decision | tags: [security, scope, roadmap] | TLS、认证、加密、重放防护、服务发现等网关层能力**不属于 uvrpc**。
- [uv-run-once-benchmark-methodology](pages/uv-run-once-benchmark-methodology.md) — category: concept | tags: [benchmark, measurement, methodology] | ## 这些数字是什么量
- [uvbus-transport-abstraction](pages/uvbus-transport-abstraction.md) — category: decision | tags: [transport, architecture, uvbus] | UVBus 是 uvrpc 的传输层：一个 `uvbus_t` 总线对象 + **一份 7 槽 vtable**，5 个传输驱动平级实现它（tcp / udp / ipc / inproc / sameloop）。
- [uvrpc-tech-stack-lineage](pages/uvrpc-tech-stack-lineage.md) — category: decision | tags: [serialization, event-loop, lineage] | ## 谱系（每步都有 commit 证据，非回忆）
- [zero-threads-locks-globals](pages/zero-threads-locks-globals.md) — category: concept | tags: [constraint, concurrency, architecture] | ## 法则表述（准确版）
