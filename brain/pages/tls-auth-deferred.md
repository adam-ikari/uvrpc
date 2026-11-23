---
id: tls-auth-deferred
title: "TLS/auth 当前不纳入生产级差距"
category: decision
status: active
tags: [security, scope, roadmap]
created: "2026-11-23T21:37:51"
updated: "2026-11-24T07:46:25"
---

<!-- compiled_truth -->
TLS、认证、加密、重放防护、服务发现等网关层能力**不属于 uvrpc**。这是库定位，不是临时延后项：uvrpc 是与 libuv 单线程 loop 契合的低层 RPC + 传输库，不是开箱即用的服务框架、网关或服务网格。安全边界由外部网关或 Sidecar 提供；不可信网络不是靠给库加 TLS 选项来解决。

现行线格式只有 `msgid/type/method/data`，TCP/UDP/IPC 传明文帧；当前适用边界为可信网络或进程内通信。只有项目定位本身改变时才重新评估这整组能力，不能把它们塞回生产级差距清单。本决策独立于 [[minimal-rpcframe-schema]] 与 [[uvbus-transport-abstraction]]。

生产就绪评估剩余关注点：发布版本落后、CI 门禁缺口、诊断语义弱。UDP 长跑容量治理已由空闲 peer 过期闭合。


## Timeline

- time: 2026-11-23T21:37:51
  kind: decision
  summary: "Created this page: TLS/auth 当前不纳入生产级差距"
  source: user decision 2026-11-23
  affects: [tls-auth-deferred]

- time: 2026-11-23T21:37:51
  kind: decision
  summary: "用户明确当前不考虑 TLS/auth，生产就绪评估不再把安全边界列为差距"
  source: user decision 2026-11-23
  affects: [tls-auth-deferred]

- time: 2026-11-24T07:46:25
  kind: decision
  summary: "TLS/auth are gateway-layer responsibilities; uvrpc is a low-level library, not out-of-box"
  source: user decision 2026-11-24
  affects: [tls-auth-deferred]

- time: 2026-11-24T07:46:25
  kind: decision
  summary: "User reaffirmed: TLS/auth are gateway-layer work; uvrpc is not an out-of-box framework, so these are non-goals rather than deferred production gaps"
  source: user decision 2026-11-24
  affects: [tls-auth-deferred]
