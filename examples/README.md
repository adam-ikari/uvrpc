# UVRPC 示例程序

本目录包含 UVRPC 的各种示例程序，展示了不同的使用场景和功能。

## 示例列表

### 基础示例

#### 1. simple_server.c / simple_client.c
最简单的服务器和客户端示例，演示基本的 RPC 调用。

```bash
# 终端 1：启动服务器
./dist/bin/simple_server

# 终端 2：运行客户端
./dist/bin/simple_client
```

#### 2. complete_example.c
完整的示例程序，展示所有功能：
- 客户端-服务器（CS）模式
- 发布-订阅（广播）模式
- 所有传输协议（TCP、UDP、IPC、INPROC）
- 多客户端并发
- 错误处理

```bash
# CS 模式
./dist/bin/complete_example server tcp://127.0.0.1:5555
./dist/bin/complete_example client tcp://127.0.0.1:5555

# 广播模式
./dist/bin/complete_example publisher udp://127.0.0.1:6000
./dist/bin/complete_example subscriber udp://127.0.0.1:6000

# 不同传输协议
./dist/bin/complete_example server ipc:///tmp/uvrpc.sock
./dist/bin/complete_example server inproc://test
```

### 传输协议示例

#### 3. tcp_rpc_demo.c
TCP 传输的 RPC 示例，展示可靠的面向连接通信。

```bash
./dist/bin/tcp_rpc_demo
```

#### 4. udp_rpc_demo.c
UDP 传输的 RPC 示例，展示无连接的高吞吐通信。

```bash
./dist/bin/udp_rpc_demo
```

#### 5. uvbus_demo.c
UVBus 传输层示例，展示底层传输层的使用。

```bash
./dist/bin/uvbus_demo
```

### 高级示例

#### 6. async_await_demo.c
异步模式示例，展示如何处理异步回调。

```bash
./dist/bin/async_await_demo
```

#### 7. async_chain_demo.c
异步链式调用示例，展示如何链式调用多个 RPC。

```bash
./dist/bin/async_chain_demo
```

#### 8. concurrent_demo.c
并发示例，展示多客户端并发访问。

```bash
./dist/bin/concurrent_demo
```

#### 9. multi_service_loop_reuse.c
多服务循环复用示例，展示如何在同一事件循环中运行多个服务。

```bash
./dist/bin/multi_service_loop_reuse
```

### 循环注入示例

#### 10. loop_injection_example.c
循环注入示例，展示如何将自定义 libuv loop 注入到 UVRPC。

```bash
./dist/bin/loop_injection_example
```

**重要性**：循环注入是 UVRPC 的核心特性之一，允许：
- 多实例独立运行（独立 loop）
- 多实例共享循环（共享 loop）
- 集成到现有的事件循环应用中

### FlatBuffers 集成示例

#### 11. flatbuffers_demo.c
FlatBuffers 深度集成示例，展示完整的 FlatBuffers DSL 使用流程。

```bash
./dist/bin/flatbuffers_demo
```

#### 12. flatbuffers_simple_demo.c
简化的 FlatBuffers 示例，适合快速入门。

```bash
./dist/bin/flatbuffers_simple_demo.c
```

#### 13. generated_client_example.c
生成的客户端代码示例，展示 FlatBuffers DSL 生成的代码。

```bash
./dist/bin/generated_client_example
```

### 消息 ID 示例

#### 15. msgid_demo.c
消息 ID 示例，展示如何使用消息 ID 进行请求匹配。

```bash
./dist/bin/msgid_demo
```

### 网关示例

#### 16. gateway_demo.c
网关示例，展示如何将 UVRPC 用作消息网关。

```bash
./dist/bin/gateway_demo
```

### 广播模式示例

#### 17. broadcast_publisher.c / broadcast_subscriber.c
**DSL 驱动的广播模式示例**，展示如何使用 DSL 生成的广播服务 API。

这两个示例使用 FlatBuffers DSL 自动生成的代码：
- `uvrpc_broadcast_service_publish_news()` - 发布新闻
- `uvrpc_broadcast_service_update_weather()` - 更新天气
- `uvrpc_broadcast_service_notify_event()` - 发送事件通知
- 解码函数用于接收和解析消息

```bash
# 终端 1：启动发布者
./dist/bin/broadcast_publisher

# 终端 2：启动订阅者
./dist/bin/broadcast_subscriber
```

**关键特性**：
- 类型安全：使用 FlatBuffers schema 定义的类型
- 无需手动编码：自动处理消息序列化/反序列化
- 易于扩展：在 `rpc_broadcast.fbs` 中添加新方法即可

### 流式响应示例

#### 19. streaming_demo.c
流式响应示例，展示服务器如何发送多个响应块。

```bash
./dist/bin/streaming_demo
```

**关键特性**：
- 多响应块：每个请求可以接收多个响应
- 流式传输：适合大文件传输或实时数据流
- 自动管理：客户端自动检测流结束

#### 20. stream_api_demo.c
**新流式 API 示例**，展示最新的流式响应 API 使用方法。

**服务器 API**：
- `uvrpc_request_send_response_more()` - 发送中间响应（type=2）
- `uvrpc_request_send_response()` - 发送最后响应（type=1）

**客户端 API**：
- `resp->frame_type` - 帧类型字段
- `uvrpc_response_is_stream_more()` - 检查是否还有更多响应
- `uvrpc_response_is_stream_end()` - 检查是否为最后响应

```bash
./dist/bin/stream_api_demo
```

**协议设计**：
- `type=2` (ResponseMore): 更多响应将到来
- `type=1` (Response): 最后响应，流结束

#### 21. test_multi_services.c
多服务测试，展示同时运行多个发布-订阅服务。

```bash
./dist/bin/test_multi_services
```

### 场景示例

#### 22. scenario_1_simple_request_response.c
**场景 1：简单的请求-响应模式**

使用场景：
- 查询操作（如用户信息查询）
- 计算操作（如加法、乘法）
- 状态查询（如服务健康检查）

特点：
- 单个请求 → 单个响应
- 最常用的 RPC 模式
- 简单直接，易于理解

```bash
# 编译
gcc -I. -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    -Lbuild -Ldeps/mimalloc/out/release \
    examples/scenario_1_simple_request_response.c \
    -o scenario_1_simple_request_response -luvrpc -luv -lmimalloc -lpthread

# 运行
./scenario_1_simple_request_response
```

#### 23. scenario_2_streaming_data_transfer.c
**场景 2：流式数据传输**

使用场景：
- 大文件传输（分块发送）
- 实时数据流（如日志、传感器数据）
- 分页查询结果
- 批量数据处理

特点：
- 单个请求 → 多个响应
- 使用 `uvrpc_request_send_response_more()` 发送中间响应
- 使用 `uvrpc_request_send_response()` 发送最后响应
- 客户端使用 `uvrpc_response_is_stream_more()` 和 `uvrpc_response_is_stream_end()` 检测

```bash
# 编译
gcc -I. -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    -Lbuild -Ldeps/mimalloc/out/release \
    examples/scenario_2_streaming_data_transfer.c \
    -o scenario_2_streaming_data_transfer -luvrpc -luv -lmimalloc -lpthread

# 运行
./scenario_2_streaming_data_transfer
```

#### 24. scenario_3_oneway_logging.c
**场景 3：单向 RPC（日志记录）**

使用场景：
- 日志记录（不需要响应）
- 事件通知（fire-and-forget）
- 心跳检测（只需发送，无需确认）
- 指标上报（异步发送）

特点：
- 只发送请求，不等待响应
- 服务器端不发送响应
- 高吞吐量，低延迟
- 使用 NULL callback 实现 oneway 模式

```bash
# 编译
gcc -I. -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    -Lbuild -Ldeps/mimalloc/out/release \
    examples/scenario_3_oneway_logging.c \
    -o scenario_3_oneway_logging -luvrpc -luv -lmimalloc -lpthread

# 运行
./scenario_3_oneway_logging
```

#### 25. scenario_4_broadcast_mode.c
**场景 4：广播模式**

使用场景：
- 消息推送（如新闻、通知）
- 事件广播（如系统告警）
- 实时状态更新（如股票价格）
- 多订阅者系统

特点：
- 一个发布者，多个订阅者
- 发布者不关心有多少订阅者
- 订阅者可以随时加入/离开
- 推荐使用 UDP 传输层

```bash
# 编译
gcc -I. -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    -Lbuild -Ldeps/mimalloc/out/release \
    examples/scenario_4_broadcast_mode.c \
    -o scenario_4_broadcast_mode -luvrpc -luv -lmimalloc -lpthread

# 运行
./scenario_4_broadcast_mode
```

#### 26. scenario_5_mixed_mode_api.c
**场景 5：混合模式 API**

使用场景：
- 需要多种 RPC 模式的复杂应用
- RESTful API 风格的服务
- 微服务架构

特点：
- 同时使用请求-响应、流式、单向、广播
- 根据业务需求选择合适的模式
- 灵活的架构设计
- 展示如何在一个应用中组合使用所有 RPC 模式

```bash
# 编译
gcc -I. -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    -Lbuild -Ldeps/mimalloc/out/release \
    examples/scenario_5_mixed_mode_api.c \
    -o scenario_5_mixed_mode_api -luvrpc -luv -lmimalloc -lpthread

# 运行
./scenario_5_mixed_mode_api
```

### RPC 调用指南

为了帮助开发者选择正确的 RPC 调用方式，我们提供了详细的调用指南：

#### RPC_CALL_GUIDE.md
完整的 RPC 调用方式选择指南，包含：

1. **快速决策树** - 根据需求快速选择合适的 RPC 模式
2. **详细场景对比** - 每种模式的使用场景、API 使用和特点
3. **传输层选择建议** - TCP/UDP/IPC/INPROC 的使用场景
4. **性能对比** - 各模式的吞吐量、延迟和可靠性对比
5. **常见错误** - 避免常见的编程错误
6. **示例程序索引** - 对应的示例程序列表

```bash
# 查看指南
cat examples/RPC_CALL_GUIDE.md
```

**决策树概览**：

```
需要发送响应吗？
├─ 否 → 使用 Oneway RPC (NULL callback)
│   - 日志记录
│   - 事件通知
│   - 心跳检测
│   └─ 示例: scenario_3_oneway_logging.c
│
└─ 是 → 需要多个响应吗？
    ├─ 否 → 使用 Request-Response (uvrpc_request_send_response)
    │   - 查询操作
    │   - 计算操作
    │   - 状态查询
    │   └─ 示例: scenario_1_simple_request_response.c
    │
    └─ 是 → 使用 Streaming (uvrpc_request_send_response_more + uvrpc_request_send_response)
        - 大文件传输
        - 实时数据流
        - 分页查询
        - └─ 示例: scenario_2_streaming_data_transfer.c
```

详细的指南请查看 [RPC_CALL_GUIDE.md](RPC_CALL_GUIDE.md)。

### RPC DSL 示例

#### 20. rpc_dsl_demo.c
RPC DSL 示例，展示如何使用 FlatBuffers DSL 定义 RPC 接口。

```bash
./dist/bin/rpc_dsl_demo
```

#### 21. rpc_dsl_usage_example.c
RPC DSL 使用示例，更详细的 DSL 用法展示。

```bash
./dist/bin/rpc_dsl_usage_example
```

#### 22. broadcast_service_demo.c
**DSL 驱动的广播服务完整示例**，展示：
- 使用 FlatBuffers schema (`rpc_broadcast.fbs`) 定义广播服务
- 自动生成的类型安全 API
- 三种不同的消息类型（新闻、天气、事件）
- 完整的发布-订阅流程

```bash
# 发布者模式
./dist/bin/broadcast_service_demo publisher

# 订阅者模式
./dist/bin/broadcast_service_demo subscriber
```

**DSL 生成的 API**（位于 `src/uvrpc_broadcast_service.h`）：
```c
// 发布函数
uvrpc_broadcast_service_publish_news()
uvrpc_broadcast_service_update_weather()
uvrpc_broadcast_service_notify_event()

// 解码函数
uvrpc_broadcast_service_decode_publish_news()
uvrpc_broadcast_service_decode_update_weather()
uvrpc_broadcast_service_decode_notify_event()
```

### Oneway RPC 示例

#### 23. test_oneway.c
Oneway RPC 基础测试示例，展示如何使用返回 `EmptyResponse` 的 oneway 方法。

```bash
# 需要先生成代码
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc \
    schema/benchmark_server.fbs -o generated

# 编译并运行
./test_oneway
```

**关键特性**：
- Oneway 方法不需要等待响应
- 无 callback 参数
- 不生成 `*_sync()` 变体
- 使用 `uvrpc_client_call_oneway()` 内部实现

#### 24. log_simple_demo.c
简单的日志服务示例，展示基础的 oneway 日志记录。

```bash
# 运行快速脚本
./scripts/run_log_service_demo.sh

# 或手动编译运行
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc \
    schema/log_service.fbs -o generated/log_service
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include \
    -Ideps/mimalloc/include -Lbuild -Ldeps/mimalloc/out/release \
    examples/log_simple_demo.c \
    generated/log_service/log_logservice_client.c \
    generated/log_service/log_logservice_server_stub.c \
    generated/log_service/log_logservice_rpc_common.c \
    -o log_simple_demo -luvrpc -luv -lmimalloc -lpthread
./log_simple_demo
```

**功能**：
- 单条日志记录
- 不同日志级别（DEBUG、INFO、WARNING、ERROR、FATAL）
- 时间戳和源信息
- 简单的服务器端格式化输出

#### 25. log_service_demo.c
完整的日志服务示例，展示分布式日志服务的所有功能。

```bash
# 手动编译运行
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc \
    schema/log_service.fbs -o generated/log_service
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include \
    -Ideps/mimalloc/include -Lbuild -Ldeps/mimalloc/out/release \
    examples/log_service_demo.c \
    generated/log_service/log_logservice_client.c \
    generated/log_service/log_logservice_server_stub.c \
    generated/log_service/log_logservice_rpc_common.c \
    -o log_service_demo -luvrpc -luv -lmimalloc -lpthread
./log_service_demo
```

**完整功能**：
- **单条日志** - 带完整元数据（级别、时间戳、源、线程ID、组件）
- **批量日志** - 一次发送多条日志记录
- **快速日志** - 简化的日志 API（仅级别和消息）
- **高吞吐量** - 10+ 条日志的并发发送演示

**使用场景**：
- 微服务集中日志
- 审计跟踪
- 指标收集
- 事件流
- 调试追踪

**Schema 定义**（`schema/log_service.fbs`）：
```flatbuffers
enum LogLevel:byte {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4
}

table LogEntry {
    level: LogLevel;
    message: string;
    timestamp: int64;
    source: string;
    thread_id: uint64;
    component: string;
}

rpc_service LogService {
    Log(LogEntry):EmptyResponse;           /* 单条日志 - oneway */
    LogBatch(LogBatchRequest):EmptyResponse; /* 批量日志 - oneway */
    QuickLog(QuickLogRequest):EmptyResponse;  /* 快速日志 - oneway */
}
```

**生成的 API**：
```c
// 所有方法都是 oneway，无 callback 参数
uvrpc_error_t uvrpc_logservice_Log(uvrpc_client_t* client,
                                    LogLevel level,
                                    const char* message,
                                    int64_t timestamp,
                                    const char* source,
                                    uint64_t thread_id,
                                    const char* component);

uvrpc_error_t uvrpc_logservice_LogBatch(uvrpc_client_t* client,
                                         const LogEntry* entries_data,
                                         size_t entries_size);

uvrpc_error_t uvrpc_logservice_QuickLog(uvrpc_client_t* client,
                                         LogLevel level,
                                         const char* message);
```

详细文档请查看 [LOG_SERVICE_README.md](LOG_SERVICE_README.md)。

### 工具和调试示例

#### 23. debug_test.c
调试工具，展示如何调试 UVRPC 应用。

```bash
./dist/bin/debug_test
```

#### 24. test_retry.c
重试机制测试，展示失败重试逻辑。

```bash
./dist/bin/test_retry
```

#### 25. rpc_user_impl.c
用户自定义实现示例，展示如何实现自定义的 RPC 处理器。

```bash
./dist/bin/rpc_user_impl
```

### 分离的 UVBus 示例

#### 26. uvbus_server_only.c
仅服务器端 UVBus 示例。

```bash
./dist/bin/uvbus_server_only
```

#### 27. uvbus_client_only.c
仅客户端 UVBus 示例。

```bash
./dist/bin/uvbus_client_only
```

#### 28. uvbus_minimal_test.c
最小化的 UVBus 测试。

```bash
./dist/bin/uvbus_minimal_test
```

#### 29. uvbus_simple_test.c
简化的 UVBus 测试。

```bash
./dist/bin/uvbus_simple_test
```

#### 30. uvbus_standalone_test.c
独立的 UVBus 测试。

```bash
./dist/bin/uvbus_standalone_test
```

#### 31. uvbus_working_example.c
工作的 UVBus 示例。

```bash
./dist/bin/uvbus_working_example
```

#### 32. uvbus_demo.c
UVBus 完整演示。

```bash
./dist/bin/uvbus_demo
```

## 编译示例

所有示例都已经包含在主构建系统中，使用以下命令编译：

```bash
# 编译所有示例
make

# 或使用构建脚本
./build.sh

# 编译完成后，示例程序位于：
./dist/bin/
```

## 运行示例

### 快速开始

最简单的示例：

```bash
# 终端 1
./dist/bin/simple_server

# 终端 2
./dist/bin/simple_client
```

### 完整功能示例

```bash
# 启动服务器
./dist/bin/complete_example server tcp://127.0.0.1:5555

# 在另一个终端运行客户端
./dist/bin/complete_example client tcp://127.0.0.1:5555
```

### 广播模式示例

```bash
# 终端 1：启动发布者
./dist/bin/complete_example publisher udp://127.0.0.1:6000

# 终端 2：启动订阅者
./dist/bin/complete_example subscriber udp://127.0.0.1:6000

# 终端 3：启动另一个订阅者
./dist/bin/complete_example subscriber udp://127.0.0.1:6000
```

### 不同传输协议

```bash
# TCP
./dist/bin/complete_example server tcp://127.0.0.1:5555

# UDP
./dist/bin/complete_example server udp://127.0.0.1:6000

# IPC
./dist/bin/complete_example server ipc:///tmp/uvrpc.sock

# INPROC
./dist/bin/complete_example server inproc://test
```

## 学习路径

### 初学者

1. **simple_server.c / simple_client.c** - 学习基本 RPC 调用
2. **flatbuffers_simple_demo.c** - 学习 FlatBuffers 基础
3. **broadcast_publisher.c / broadcast_subscriber.c** - 学习 DSL 驱动的发布-订阅模式

### 进阶用户

1. **complete_example.c** - 学习所有功能
2. **async_await_demo.c** - 学习异步处理
3. **loop_injection_example.c** - 学习循环注入
4. **broadcast_service_demo.c** - 学习完整的 DSL 广播服务

### 高级用户

1. **flatbuffers_demo.c** - 学习 FlatBuffers 深度集成
2. **rpc_dsl_demo.c** - 学习 RPC DSL
3. **rpc_dsl_usage_example.c** - 学习 DSL 用法
4. **concurrent_demo.c** - 学习并发处理
5. **gateway_demo.c** - 学习网关模式

### 性能调优

1. **allocator_demo.c** - 内存分配器
2. **multi_service_loop_reuse.c** - 循环复用
3. **uvasync_demo.c** - 异步编程原语

（曾有一个 `perf_mode_demo.c` 演示性能模式开关；那个开关只是被存下来从未生效，已连同 API 一起删除。要做吞吐调优请看 [Benchmark](/guide/benchmark) 与
`uvrpc_client_call_batch()`。）

## 常见问题

### Q: 如何选择合适的示例？

**A**: 根据你的需求：
- 初学者：从 simple_server/client 开始
- 需要 RPC：查看 complete_example.c
- 需要广播：查看 broadcast_publisher/subscriber.c（DSL 驱动）
- 需要 DSL：查看 broadcast_service_demo.c、rpc_dsl_demo.c
- 需要高性能：查看 benchmark/ 与 sameloop_rpc_demo.c
- 需要集成到现有应用：查看 loop_injection_example.c

### Q: 什么是 DSL 驱动的 API？

**A**: DSL（领域特定语言）驱动的 API 是通过 FlatBuffers schema 自动生成的：
- 在 `schema/rpc_broadcast.fbs` 中定义服务接口
- FlatCC 编译器自动生成类型安全的 API
- 开发者无需手动编写序列化/反序列化代码
- 修改 schema 后重新生成即可更新 API

相关示例：
- **broadcast_publisher.c / broadcast_subscriber.c** - DSL 广播示例
- **broadcast_service_demo.c** - 完整 DSL 服务演示
- **rpc_dsl_demo.c** - RPC DSL 示例

### Q: 示例程序可以用于生产环境吗？

**A**: 示例程序主要用于学习和演示，生产环境使用时请：
- 添加完整的错误处理
- 实现日志记录
- 添加监控和指标
- 进行充分的测试

### Q: 如何修改示例以适应我的需求？

**A**: 示例程序设计为易于修改：
1. 复制示例代码
2. 修改地址、处理器、回调等
3. 根据需求添加功能
4. 参考文档进行优化

### Q: 示例程序的性能如何？

**A**: 示例程序使用默认配置，实际性能取决于：
- 传输协议选择
- 客户端数量
- 批处理大小
- 系统资源

详细的性能数据请查看 [benchmark/results/](../benchmark/results/)。

## 贡献

欢迎贡献新的示例程序！请遵循以下指南：

1. 示例应该清晰、简洁、有教育意义
2. 添加适当的注释
3. 更新本 README 文件
4. 确保示例可以编译和运行

## 联系方式

如有问题或建议，请：
- 提交 Issue
- 发起 Pull Request
- 查看文档目录

---

**最后更新**: 2026-02-21  
**版本**: 0.1.0