---
id: codegen-shape-matrix
title: "代码生成器必须被每种形状实例化：schema 是测试夹具，不是示例"
category: decision
status: active
tags: [codegen, dsl, flatcc, testing]
created: "2026-11-10T11:25:44"
updated: "2026-11-10T11:25:55"
---

<!-- compiled_truth -->
生成模板是按形状分支展开的（标量数组 / 字符串向量 / 表数组 × 请求 / 响应 × _sync / _all / _any / 批量），任何一支没有 schema 实例化，它就只是"写出来但从未编译过"的文本。真实的两次事故都是这个模式：`[Table]` 响应（元素结构体不发射、用 uint8 接表向量、访问器传 &item）和"宽度大于 1 字节的标量向量响应/请求"（请求用 flatbuffers_uint8_vec_create 只拷 N 字节、响应用裸指针强转只在小端成立），都在 200+ 测试全绿的情况下存在了很久，因为树里只有 kv.fbs（无数组）、strings.fbs（[string]）、table_arrays.fbs（[Table]）——没有一个非 ubyte 的标量向量。

规则：每新增一种模板形状分支，就必须在同一 commit 里加一个能编译它的 schema + 测试；形状用 tests/schema/shapes.fbs 那张"矩阵表"扩充（一张表里同时放 int32/double/bool 标量向量、[string]、[Table]），测试用 SAMELOOP 覆盖 _sync/_all/_any 与 response_free/result_free/空向量。判定标准不是"测试通过"，而是"删掉这一支的实现，测试会红"。

配套：flatcc 的命名由元素决定，模板必须用 parser 给出的字段元数据（flatcc_type / flatcc_vec_type）而不是硬编码：标量向量是 flatbuffers_<T>_vec_t + _vec_at + <Table>_<field>_create；表向量是 {ns}_{T}_vec_t；字符串向量是 flatbuffers_string_vec_t。派生元数据加在 RPCParser._parse_fields 里，schema 里出现的字段名/类型不要出现在模板字符串中。


## Timeline

- time: 2026-11-10T11:25:44
  kind: decision
  summary: "Created this page: 代码生成器必须被每种形状实例化：schema 是测试夹具，不是示例"
  source: "tools/uvrpcc.py, tests/schema/shapes.fbs, tests/generated_shapes_test.c"
  affects: [codegen-shape-matrix]

- time: 2026-11-10T11:25:55
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "tools/uvrpcc.py, tools/templates/, tests/schema/shapes.fbs"
  affects: [codegen-shape-matrix]
