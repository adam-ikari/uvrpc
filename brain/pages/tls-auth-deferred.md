---
id: tls-auth-deferred
title: "TLS/auth 当前不纳入生产级差距"
category: decision
status: active
tags: [security, scope, roadmap]
created: "2026-11-23T21:37:51"
updated: "2026-11-23T21:37:51"
---

<!-- compiled_truth -->
当前阶段**不考虑 TLS、认证、加密或重放防护**，这些不列入生产级差距清单。这是范围决策，不是对 [[minimal-rpcframe-schema]] 或 [[uvbus-transport-abstraction]] 的永久 Non-goal 声明：将来若要进入不可信网络，必须重新打开这个决策并设计完整方案。

现行线格式只有 `msgid/type/method/data`，TCP/UDP/IPC 传明文帧。因此当前适用边界是**可信网络或进程内通信**；不可信网络需要由外部网关或 Sidecar 提供安全边界，而不是假设库本身具备。

生产就绪评估剩余关注点：UDP 长跑容量治理、发布版本落后、CI 门禁缺口、诊断语义弱。


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
