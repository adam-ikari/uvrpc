---
id: oneway-is-declared-not-inferred
title: "oneway 由方法上的 (oneway) 属性声明，不再从响应表名反推"
category: decision
status: active
tags: [codegen, dsl, oneway, api]
created: "2026-11-10T11:26:03"
updated: "2026-11-10T11:26:03"
---

<!-- compiled_truth -->
oneway 的判据从"响应表名叫 EmptyResponse"改成方法声明上的 `(oneway)` 属性。旧规则把协议语义挂在表名上：改名即改线格式，而且生成器第 227 行本来就会剥掉方法上的 `(xxx)` 属性、却只用了 (stream)，(oneway) 被扔掉后再靠名字反推。

实现要点：uvrpcc.py 在剥离属性之前读 `is_oneway = '(oneway)' in line`（与 is_stream 同一处）；flatcc 不接受未声明的自定义属性，所以用到它的 schema 必须写 `attribute "oneway";`（否则 flatcc 报 unknown attribute not declared）。仓库内所有原本靠名字被判成 oneway 的方法（log_service 的 Log/LogBatch/QuickLog、benchmark_server 的 Log、shop 的 Record）都已在 schema 中显式声明；benchmark.fbs 的 Echo(EmptyRequest):EmptyResponse 本来就没有调用方、注释说它是 latency 测试，因此按普通请求-响应处理，没有加属性。


## Timeline

- time: 2026-11-10T11:26:03
  kind: decision
  summary: "Created this page: oneway 由方法上的 (oneway) 属性声明，不再从响应表名反推"
  source: "tools/uvrpcc.py, schema/shop.fbs"
  affects: [oneway-is-declared-not-inferred]

- time: 2026-11-10T11:26:03
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "tools/uvrpcc.py, schema/*.fbs"
  affects: [oneway-is-declared-not-inferred]
