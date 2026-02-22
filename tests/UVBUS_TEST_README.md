# UVBus 测试用例说明

## 概述

本文档描述了 UVBus 层的单元测试和性能测试的详细说明。

## 测试文件

### 1. 单元测试 (tests/uvbus_test.c) - ✅ 完全可用

#### 测试框架

#### 测试框架
- 使用纯 C 标准库编写
- 使用 `assert` 宏进行结果验证
- 不依赖外部测试框架
- 独立的测试计数和报告系统

#### 测试用例列表

| 测试用例 | 描述 | 验证内容 |
|---------|------|---------|
| `test_config_create_free` | 创建和释放配置 | 配置对象创建和释放不崩溃 |
| `test_config_set_get` | 设置和获取配置参数 | 循环、传输类型、地址、超时正确设置 |
| `test_config_callbacks` | 设置所有回调 | 接收、连接、关闭、错误回调正确设置 |
| `test_server_create_free` | 创建和释放服务器 | 服务器对象创建、标志正确、资源释放 |
| `test_server_null_config` | NULL 配置创建服务器 | 正确返回 NULL |
| `test_client_create_free` | 创建和释放客户端 | 客户端对象创建、标志正确、资源释放 |
| `test_client_connect_no_server` | 无服务器时客户端连接 | 不崩溃，正确处理错误 |
| `test_server_listen_stop` | 服务器监听和停止 | 成功监听和停止 |
| `test_invalid_params` | 无效参数错误处理 | NULL 参数正确返回错误码 |
| `test_client_send_unconnected` | 未连接客户端发送数据 | 正确返回 NOT_CONNECTED 错误 |
| `test_server_send_inactive` | 非活动服务器发送数据 | 正确返回 NOT_CONNECTED 错误 |
| `test_memory_leak` | 内存泄漏测试 | 多次创建/释放无泄漏 |
| `test_config_timeout` | 超时配置 | 默认超时、自定义超时、启用/禁用 |
| `test_get_transport_type` | 获取传输类型 | TCP/UDP 类型正确返回 |
| `test_callback_mechanism` | 回调机制 | 所有回调函数正确调用 |
| `test_empty_address` | 空地址处理 | 空地址不崩溃 |
| `test_null_address` | NULL 地址处理 | NULL 地址不崩溃 |
| `test_pump_interval` | Pump 间隔配置 | Pump 间隔正确设置 |
| `test_server_disconnect` | 服务器断开 | 服务器忽略断开操作 |
| `test_multiple_configs` | 多个配置对象 | 多个配置对象独立工作 |

#### 测试覆盖范围

- **配置管理**: 创建、设置、释放
- **服务器操作**: 创建、监听、停止、发送
- **客户端操作**: 创建、连接、断开、发送
- **错误处理**: 无效参数、未连接、资源不足
- **内存管理**: 创建/释放循环、无泄漏
- **回调机制**: 接收、连接、关闭、错误回调
- **边界条件**: NULL 指针、空字符串、多次操作

### 2. 性能测试 (tests/uvbus_perf_test.c) - ⚠️ 需要完善

**注意**: 当前性能测试存在以下限制：
- 服务器回显机制需要进一步完善
- 客户端可能无法正确接收响应
- 建议先运行单元测试验证基本功能

#### 测试框架
- 使用纯 C 标准库编写
- 使用 `gettimeofday` 进行精确计时
- 计算吞吐量、延迟和百分位数
- 支持预热和测量阶段

#### 性能指标

| 指标 | 说明 |
|-----|------|
| `throughput_rps` | 每秒请求数 (Requests Per Second) |
| `avg_latency_ms` | 平均延迟 (毫秒) |
| `min_latency_ms` | 最小延迟 (毫秒) |
| `max_latency_ms` | 最大延迟 (毫秒) |
| `p50_latency_ms` | 50% 百分位延迟 (毫秒) |
| `p95_latency_ms` | 95% 百分位延迟 (毫秒) |
| `p99_latency_ms` | 99% 百分位延迟 (毫秒) |
| `total_time_sec` | 总测试时间 (秒) |
| `throughput_mbps` | 数据吞吐量 (Mbps) |
| `total_bytes` | 总传输字节数 |

#### 性能测试用例

| 测试用例 | 请求数 | 负载大小 | 客户端数 | 预热 | 目标 |
|---------|--------|---------|---------|------|------|
| `perf_test_small_payload` | 10,000 | 64 bytes | 1 | 100 | 小负载吞吐量 |
| `perf_test_medium_payload` | 5,000 | 1 KB | 1 | 100 | 中等负载吞吐量 |
| `perf_test_large_payload` | 1,000 | 64 KB | 1 | 10 | 大负载吞吐量 |
| `perf_test_concurrent_connections` | 5,000 | 256 bytes | 10 | 100 | 并发连接性能 |
| `perf_test_latency` | 1,000 | 32 bytes | 1 | 50 | 延迟测量 |

#### 测试流程

1. **初始化阶段**
   - 创建事件循环
   - 设置测试参数
   - 分配内存资源

2. **服务器设置**
   - 创建服务器配置
   - 启动监听
   - 设置接收回调

3. **客户端设置**
   - 创建多个客户端
   - 连接到服务器
   - 等待连接建立

4. **预热阶段**
   - 发送预热请求
   - 等待预热完成
   - 重置计数器

5. **测量阶段**
   - 记录开始时间
   - 发送测试请求
   - 记录每个请求的发送时间
   - 等待所有响应
   - 记录结束时间

6. **结果计算**
   - 计算总时间
   - 计算吞吐量
   - 计算延迟统计
   - 计算百分位数

7. **清理阶段**
   - 断开客户端连接
   - 停止服务器
   - 释放资源
   - 关闭事件循环

## 运行测试

### 编译测试

```bash
# 进入项目根目录
cd /home/zhaodi-chen/project/uvrpc

# 配置 CMake
cmake -S . -B build -DUVRPC_BUILD_TESTS=ON

# 编译
cmake --build build
```

### 运行单元测试

```bash
# 运行单元测试
./dist/bin/uvbus_test

# 或使用 CTest
cd build
ctest -R uvbus_unit_tests -V
```

### 运行性能测试

```bash
# 运行性能测试（当前版本可能需要完善）
./dist/bin/uvbus_perf_test

# 或使用 CTest
cd build
ctest -R uvbus_perf_tests -V
```

### 运行简单测试（推荐）

```bash
# 运行简单功能测试
./dist/bin/uvbus_simple_test

# 运行简单性能测试
./dist/bin/uvbus_perf_test_simple
```

### 运行所有测试

```bash
# 运行所有 UVBus 测试
cd build
ctest -R uvbus -V

# 运行所有测试
ctest -V
```

## 测试输出

### 单元测试输出

```
========================================
  UVBus Unit Tests
========================================

[TEST] Config create and free
[PASS] test_config_create_free

[TEST] Config set and get parameters
[PASS] test_config_set_get

...

========================================
  Test Summary
========================================
Total: 20
Passed: 20
Failed: 0
========================================
```

### 性能测试输出

```
========================================
  UVBus Performance Tests
========================================

[INFO] Running small payload throughput test...
[INFO] Server listening on tcp://127.0.0.1:15556
[INFO] Waiting for 1 clients to connect...
[INFO] Clients connected
[INFO] Warmup: sending 100 requests...
[INFO] Warmup complete
[INFO] Starting performance test: 10000 requests, 64 bytes payload
[INFO] Waiting for responses...
[INFO] Received 10000/10000 responses

========================================
  Small Payload Throughput (64 bytes)
========================================
Test Parameters:
  Requests: 10000
  Payload size: 64 bytes
  Clients: 1

Results:
  Total time: 0.123 seconds
  Throughput: 81300 req/s
  Data rate: 0.417 Mbps
  Total bytes: 1280000

Latency:
  Average: 0.012 ms
  Min: 0.005 ms
  Max: 0.045 ms
  P50: 0.010 ms
  P95: 0.020 ms
  P99: 0.030 ms
========================================
```

## 测试注意事项

1. **端口冲突**
   - 单元测试使用端口 15555
   - 性能测试使用端口 15556
   - 确保这些端口未被占用

2. **资源限制**
   - 性能测试需要足够的内存
   - 大负载测试可能需要 100MB+ 内存

3. **时间精度**
   - 延迟测量使用微秒级精度
   - 系统负载可能影响测量结果

4. **网络环境**
   - 测试使用本地回环地址 (127.0.0.1)
   - 结果可能因系统配置而异

5. **并发测试**
   - 并发连接测试创建 10 个客户端
   - 确保系统支持足够的文件描述符

## 预期结果

### 单元测试

- 所有 20 个测试用例应该通过
- 无内存泄漏
- 无崩溃或段错误

### 性能测试

预期性能指标（在典型的开发机器上）：

| 测试 | 吞吐量 | 平均延迟 | P95 延迟 |
|-----|--------|---------|---------|
| 小负载 (64B) | > 50,000 req/s | < 0.1 ms | < 0.2 ms |
| 中负载 (1KB) | > 10,000 req/s | < 0.5 ms | < 1.0 ms |
| 大负载 (64KB) | > 500 req/s | < 20 ms | < 40 ms |
| 并发 (10 客户) | > 40,000 req/s | < 0.2 ms | < 0.5 ms |
| 延迟 (32B) | - | < 0.05 ms | < 0.1 ms |

*注意：实际性能取决于硬件配置和系统负载*

## 故障排除

### 编译错误

```bash
# 确保 uvrpc 库已编译
cmake --build build --target uvrpc

# 检查依赖
ls deps/libuv/lib
ls deps/flatcc/lib
```

### 运行时错误

```bash
# 检查端口占用
netstat -tuln | grep 15555
netstat -tuln | grep 15556

# 检查文件描述符限制
ulimit -n

# 增加文件描述符限制（如果需要）
ulimit -n 4096
```

### 性能测试超时

- 减少请求数量
- 增加超时时间
- 检查系统负载

## 贡献

添加新测试时，请遵循以下准则：

1. 测试函数命名：`test_<功能描述>`
2. 使用 `TEST_START` 和 `TEST_PASS`/`TEST_FAIL` 宏
3. 添加适当的清理代码
4. 更新本文档
5. 确保 CMakeLists.txt 包含新测试

## 许可证

MIT License - 与 UVRPC 项目相同