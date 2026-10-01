# UVRPC 示例程序

本目录是 UVRPC 的示例源码。**只有被 CMake 注册的示例才会出现在 `dist/bin/`** ——
下面每一节列出的都是当前真正能构建、能运行的；文末单列了仓库里有源码但没有构建目标的
文件，避免按名字去找一个不存在的二进制。

## 构建与运行

```bash
./scripts/setup_deps.sh          # 首次：拉取并构建 vendored 依赖
cmake -S . -B build -DUVRPC_BUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
./dist/bin/<示例名>
```

示例全部静态链接 `libuvrpc`，不需要额外的运行期环境。

## 基础 RPC

| 示例 | 说明 |
| --- | --- |
| `simple_server.c` / `simple_client.c` | 最小的一对服务器与客户端，跑通一次请求-响应 |
| `test_oneway.c` | oneway（不等待响应）方法的调用方式 |
| `msgid_demo.c` | msgid 的生成与请求匹配 |

```bash
# 终端 1
./dist/bin/simple_server
# 终端 2
./dist/bin/simple_client
```

## 传输协议

每个示例把地址换成 `tcp://` / `udp://` / `ipc://` / `inproc://` / `sameloop://` 即可切换传输。

| 示例 | 说明 |
| --- | --- |
| `udp_rpc_demo.c` | UDP：无连接、可丢包 |
| `sameloop_rpc_demo.c` | SAMELOOP：同 loop 内的零锁快路径 |
| `simple_inproc.c` | INPROC：同 loop 内的命名端点 |
| `uvbus_demo.c` | 直接用 UVBus 传输层，不经过 RPC 层 |

## 流式响应

| 示例 | 说明 |
| --- | --- |
| `streaming_demo.c` | 服务端用 `uvrpc_request_send_response_more()` 连发多帧，客户端用 `uvrpc_response_is_stream_more()` / `..._is_stream_end()` 判断结束 |

## 异步与并发

| 示例 | 说明 |
| --- | --- |
| `async_await_demo.c` | 异步回调 |
| `async_chain_demo.c` | 链式调用 |
| `concurrent_demo.c` | 多客户端并发 |
| `primitives_demo.c` | `uvrpc_primitives.h` 的 promise / 组合子 |

Promise 组合子另有独立测试目标：`test_promise_basic`、`test_promise_combinators`、
`test_promise_race_simple`、`test_promise_race_only`、`test_promise_race_debug`、
`test_primitives_simple`。异步原语（`uvasync.h`）的例子是 `uvasync_demo`、
`uvasync_minimal_test`。

## 事件循环

| 示例 | 说明 |
| --- | --- |
| `loop_injection_example.c` | 把自己的 `uv_loop_t` 注入 UVRPC，支持多实例共享或不共享 loop |
| `test_sameloop.c` | SAMELOOP 基本往返 |
| `test_sameloop_multiclient.c` | 同一 loop 上多个客户端 |
| `test_sameloop_recursion.c` / `test_sameloop_stress.c` | 递归调用与压力场景 |

> **用 INPROC / SAMELOOP 需要创建注册表**：`uvbus_loop_registry_new()` 得到一个
> `uvbus_loop_registry_t*`，用 `uvrpc_config_set_loop_registry()` 传给**每一个需要互相
> 通信的 config**，用完 `uvbus_loop_registry_free()` 释放。框架不在 loop 上留任何状态，
> 所以 `uv_loop_t loop = {0};` 与否都不再影响这两个传输。详见
> [Architecture](/architecture/)。

## FlatBuffers / RPC DSL

| 示例 | 说明 |
| --- | --- |
| `flatbuffers_simple_demo.c` | FlatBuffers 入门 |
| `rpc_dsl_demo.c` | 用 `tools/uvrpcc.py` 生成的类型安全 RPC API |
| `gateway_demo.c` | 把 UVRPC 当消息网关 |
| `debug_test.c` | 调试辅助 |

## 内存分配器

`allocator_demo.c` 演示 `uvrpc_alloc()` / `uvrpc_free()`。

三个编译期分配器（`system` / `mimalloc`（默认） / `custom`）里，只有 `custom` 能暴露
跨堆释放：`uvrpc_free()` 变成一个只认自己发出去的指针的池。`tests/allocator_ownership_test.c`
在 `custom` 构建下用它守住"flatcc 的帧缓冲区只能用 `free()` 释放"这条边界。

## UVBus（独立于 RPC 层）

| 示例 | 说明 |
| --- | --- |
| `uvbus_server_only.c` | 只起服务端 |
| `uvbus_client_only.c` | 只起客户端 |
| `uvbus_broadcast_test` / `uvbus_simple_test` / `uvbus_unit_tests` | 传输层测试（在 `tests/` 下构建） |

## 广播

RPC 层没有"发布-订阅"这种 API，广播是传输层能力：`uvbus_broadcast()`
（`include/uvbus.h`）。发布者不关心订阅者数量，订阅者可以随时进出；UDP 是它的典型用法。
参考实现见 `examples/scenario_4_broadcast_mode.c`（当前不在构建目标里，见下）。

## 场景演示与 uvbus 级示例

这 13 个此前没有构建目标，即"写完就再没人编译过"。它们能跑通，但没有输出说明；下面是它们
各自的用途。

| 示例 | 说明 |
| --- | --- |
| `scenario_1_simple_request_response.c` | 场景 1：最小请求-响应 |
| `scenario_2_streaming_data_transfer.c` | 场景 2：流式传输数据 |
| `scenario_3_oneway_logging.c` | 场景 3：oneway 发日志，不等响应 |
| `scenario_4_broadcast_mode.c` | 场景 4：广播模式 |
| `scenario_5_mixed_mode_api.c` | 场景 5：三种模式混用 |
| `stream_api_demo.c` / `simple_stream_dsl.c` | 流式 API 的两种写法 |
| `test_10_requests.c` | 连续 10 次请求 |
| `test_simple_server.c` | 只启服务器，配套单独的客户端 |
| `test_semaphore.c` / `test_semaphore2.c` | 信号量两种用法 |
| `uvbus_minimal_test.c` / `uvbus_standalone_test.c` / `uvbus_working_example.c` | uvbus 传输层脱离 uvrpc 单独使用 |

```bash
# 终端 1
./dist/bin/scenario_1_simple_request_response
# 终端 2
./dist/bin/simple_client
```

## 仓库里有源码但当前没有构建目标的示例

这 4 个文件存在于 `examples/`，但没有对应的 `dist/bin/` 二进制。**不要按名字去找它们**
—— 没有开关能打开，每个都有具体障碍：

| 示例 | 障碍 |
| --- | --- |
| `rpc_user_impl.c` | 它是"填入你的服务逻辑"的实现片段，**故意没有 `main`**，单独链接不成程序。它的正确用法是连同生成的 `_server_stub.c` 一起编进你自己的可执行文件 |
| `stream_dsl_demo.c` | 使用生成器并不产出的类型 `uvrpc_stream_StreamRequest_t`（生成的是 `_ref_t`）。这是示例与生成器契约不一致，不是漏生成 |
| `generated_client_example.c`、`test_retry.c` | 需要 `rpc_benchmark_builder.h`，但仓库里**没有** `rpc_benchmark.fbs` —— 这个 schema 从未存在，所以没有任何东西能生成它 |

## 已修复的同类问题

历史上这里有 23 个。现已修复 19 个，其中值得记录的：

- **13 个只是从未注册**，编译零错误，缺的是 `create_example_target(...)`
- `scenario_4_broadcast_mode.c` 的 `printf` 参数列表里误插了一行 `fflush(stdout);`，是语法错误，从未编译过
- `multi_service_loop_reuse.c`、`test_multi_services.c` 漂移于生成 API：`create_server` 增加了 registry 参数、`create_client` 增加了 connect callback
- `flatbuffers_demo.c` 需要 `rpc_example.fbs` 的 flatcc 读码器，而构建从未为它生成
- `rpc_dsl_usage_example.c` 混用了三套不一致的 API，包括 `rpc_register_all()` 和 `MathService_Add()` —— **这两个函数在生成器和模板里都不存在**，是照着从未实现的特性写的

最后两条的根因值得记住：`generate_dsl` 目标只覆盖了 `log_service.fbs` 一个 schema，而生成器**没有剥离注释**
—— 它把 schema 里那段讲语法的 `/* rpc_service ServiceName { ... } */` 当成真service，生成出无法编译的代码。
两处都已修在根上：DSL 生成泛化到多个 schema，解析前统一剥离注释。

**注册本身就是防腐化手段** —— 今天验收套件挖出的每个真实缺陷，根因都是"没有测试会编译这段代码"。

## 配套文档

- [RPC_CALL_GUIDE.md](RPC_CALL_GUIDE.md) —— 请求-响应 / 流式 / oneway 的选择依据
- [INPROC_README.md](INPROC_README.md) —— INPROC 传输
- [LOG_SERVICE_README.md](LOG_SERVICE_README.md) —— 日志服务示例（`schema/log_service.fbs`）
- `../benchmark/` —— 性能基准，用法见 [Benchmark](/guide/benchmark)

## 学习路径

**入门**：`simple_server.c` / `simple_client.c` → `test_oneway.c` → `streaming_demo.c`

**进阶**：`loop_injection_example.c` → `async_await_demo.c` → `concurrent_demo.c` →
`gateway_demo.c`

**深入**：`rpc_dsl_demo.c` → `uvbus_demo.c` → `sameloop_rpc_demo.c` → `benchmark/`

**调优**：`allocator_demo.c` → [Benchmark](/guide/benchmark) →
`uvrpc_client_call_batch()`

> 曾有一个 `perf_mode_demo.c` 演示"性能模式开关"。那个开关只是被存下来、从未生效，
> 已连同 API 一起删除，不要按这个名字找。

## 常见问题

**Q：示例能直接用于生产吗？**
不能。示例只演示 API 用法，缺错误处理、日志、监控与测试。

**Q：怎么改造成自己的服务？**
复制最接近的示例，换掉地址、handler 与 callback；handler 里 `req->params` 只在回调期间
有效，要跨回调保留必须自己拷贝（`include/uvrpc.h` 的 IMPORTANT 块）。

**Q：性能怎么测？**
用 `benchmark/` 而不是示例程序。测出来的数字只有在"顺序 ping-pong + 日志关闭 +
`UV_RUN_ONCE`"的前提下才有意义，UDP 的数字只在本机 loopback 上成立。

## 贡献新示例

1. 示例要能独立跑通，且只依赖公开 API；
2. 在 `CMakeLists.txt` 的 `create_example_target(...)` 列表里注册，否则不会被构建；
3. 在本页对应分类里加一行；
4. 跑一遍 `ctest` 确认没有回归。
