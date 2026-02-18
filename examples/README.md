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

### 性能模式示例

#### 14. perf_mode_demo.c
性能模式示例，展示如何在高吞吐和低延迟模式之间切换。

```bash
./dist/bin/perf_mode_demo
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
发布-订阅模式的示例。

```bash
# 终端 1：启动发布者
./dist/bin/broadcast_publisher

# 终端 2：启动订阅者
./dist/bin/broadcast_subscriber
```

#### 18. test_multi_services.c
多服务测试，展示同时运行多个发布-订阅服务。

```bash
./dist/bin/test_multi_services
```

### RPC DSL 示例

#### 19. rpc_dsl_demo.c
RPC DSL 示例，展示如何使用 FlatBuffers DSL 定义 RPC 接口。

```bash
./dist/bin/rpc_dsl_demo
```

#### 20. rpc_dsl_usage_example.c
RPC DSL 使用示例，更详细的 DSL 用法展示。

```bash
./dist/bin/rpc_dsl_usage_example
```

### 工具和调试示例

#### 21. debug_test.c
调试工具，展示如何调试 UVRPC 应用。

```bash
./dist/bin/debug_test
```

#### 22. test_retry.c
重试机制测试，展示失败重试逻辑。

```bash
./dist/bin/test_retry
```

#### 23. rpc_user_impl.c
用户自定义实现示例，展示如何实现自定义的 RPC 处理器。

```bash
./dist/bin/rpc_user_impl
```

### 分离的 UVBus 示例

#### 24. uvbus_server_only.c
仅服务器端 UVBus 示例。

```bash
./dist/bin/uvbus_server_only
```

#### 25. uvbus_client_only.c
仅客户端 UVBus 示例。

```bash
./dist/bin/uvbus_client_only
```

#### 26. uvbus_minimal_test.c
最小化的 UVBus 测试。

```bash
./dist/bin/uvbus_minimal_test
```

#### 27. uvbus_simple_test.c
简化的 UVBus 测试。

```bash
./dist/bin/uvbus_simple_test
```

#### 28. uvbus_standalone_test.c
独立的 UVBus 测试。

```bash
./dist/bin/uvbus_standalone_test
```

#### 29. uvbus_working_example.c
工作的 UVBus 示例。

```bash
./dist/bin/uvbus_working_example
```

### 内存分配器示例

#### 30. allocator_demo.c
内存分配器示例，展示如何使用不同的内存分配器。

```bash
./dist/bin/allocator_demo
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
3. **broadcast_publisher.c / broadcast_subscriber.c** - 学习发布-订阅模式

### 进阶用户

1. **complete_example.c** - 学习所有功能
2. **async_await_demo.c** - 学习异步处理
3. **loop_injection_example.c** - 学习循环注入
4. **perf_mode_demo.c** - 学习性能优化

### 高级用户

1. **flatbuffers_demo.c** - 学习 FlatBuffers 深度集成
2. **rpc_dsl_demo.c** - 学习 RPC DSL
3. **concurrent_demo.c** - 学习并发处理
4. **gateway_demo.c** - 学习网关模式

### 性能调优

1. **perf_mode_demo.c** - 性能模式
2. **allocator_demo.c** - 内存分配器
3. **multi_service_loop_reuse.c** - 循环复用

## 常见问题

### Q: 如何选择合适的示例？

**A**: 根据你的需求：
- 初学者：从 simple_server/client 开始
- 需要 RPC：查看 complete_example.c
- 需要广播：查看 broadcast_publisher/subscriber.c
- 需要高性能：查看 perf_mode_demo.c
- 需要集成到现有应用：查看 loop_injection_example.c

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

**最后更新**: 2026-02-18  
**版本**: 0.1.0