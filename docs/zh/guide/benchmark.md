# 性能测试

UVRPC 提供统一的性能测试工具 `benchmark`，支持多种测试场景和传输协议。

## Benchmark 工具

`benchmark` 程序是一个统一的性能测试工具，支持以下功能：

- **CS 模式（客户端-服务器）**：测试请求-响应模式的性能
- **广播模式（发布-订阅）**：测试发布-订阅模式的性能
- **多线程/多进程测试**：支持并发测试
- **延迟测试**：测量请求-响应延迟
- **多种传输协议**：TCP、UDP、IPC、INPROC

## 使用方法

### 启动服务器（CS 模式）

```bash
# 基本用法
./dist/bin/benchmark --server

# 指定地址
./dist/bin/benchmark --server -a tcp://127.0.0.1:5555

# 设置自动关闭超时（毫秒）
./dist/bin/benchmark --server --server-timeout 5000
```

### 运行客户端测试（CS 模式）

```bash
# 单客户端测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555

# 指定测试时长（毫秒）
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000

# 指定批处理大小
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -b 100

# 多客户端测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -c 10

# 多线程测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 5 -c 2

# 低延迟模式
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -l

# 延迟测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --latency

# 多进程测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --fork -t 3 -c 2
```

### 启动发布者（广播模式）

```bash
# 基本用法
./dist/bin/benchmark --publisher

# 指定地址
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000

# 多发布者测试
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 3

# 多线程多发布者
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -t 3 -p 2

# 指定批处理大小和时长
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -b 20 -d 5000
```

### 启动订阅者（广播模式）

```bash
# 基本用法
./dist/bin/benchmark --subscriber

# 指定地址
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000

# 多订阅者测试
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -s 5

# 多线程多订阅者
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -t 3 -s 2
```

## 参数说明

### 模式参数

- `--server`：服务器模式（CS 模式）
- `--publisher`：发布者模式（广播模式）
- `--subscriber`：订阅者模式（广播模式）

### 通用参数

- `-a <address>`：服务器/发布者地址（默认：tcp://127.0.0.1:5555）
- `-t <threads>`：线程/进程数（默认：1）
- `-b <concurrency>`：批处理大小（默认：100）
- `-d <duration>`：测试时长（毫秒，默认：1000）
- `-l`：启用低延迟模式（默认：高吞吐）
- `--latency`：运行延迟测试（忽略 -t 和 -c）
- `--fork`：使用 fork 模式代替线程（多进程测试）
- `-h`：显示帮助信息

### CS 模式参数

- `-c <clients>`：每个线程/进程的客户端数（默认：1）

### 广播模式参数

- `-p <publishers>`：每个线程/进程的发布者数（广播模式，默认：1）
- `-s <subscribers>`：每个线程/进程的订阅者数（广播模式，默认：1）

### 服务器参数

- `--server-timeout <ms>`：服务器自动关闭超时（默认：0，不超时）

## 测试场景示例

### 1. 基本吞吐量测试

```bash
# 启动服务器
./dist/bin/benchmark --server -a tcp://127.0.0.1:5555

# 在另一个终端运行客户端
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000 -b 100
```

### 2. 多客户端并发测试

```bash
# 10 个客户端并发测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -c 10 -d 2000
```

### 3. 多线程测试

```bash
# 5 个线程，每个线程 2 个客户端
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 5 -c 2 -d 2000
```

### 4. 延迟测试

```bash
# 测试请求-响应延迟
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --latency
```

### 5. 广播模式测试

```bash
# 启动发布者
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 3 -b 20 -d 5000

# 在另一个终端启动订阅者
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -s 5 -d 5000
```

### 6. 多传输协议测试

```bash
# TCP 测试
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000

# IPC 测试
./dist/bin/benchmark -a ipc:///tmp/uvrpc_test.sock -d 2000

# UDP 测试
./dist/bin/benchmark -a udp://127.0.0.1:5556 -d 2000

# INPROC 测试
./dist/bin/benchmark -a inproc://test -d 2000
```

## 性能指标

### 吞吐量指标

- **Ops/s**：每秒操作数（CS 模式）
- **Msgs/s**：每秒消息数（广播模式）
- **带宽**：数据传输速率（MB/s）

### 延迟指标

- **平均延迟**：所有请求的平均响应时间
- **P50 延迟**：中位数延迟
- **P95 延迟**：95% 的请求延迟
- **P99 延迟**：99% 的请求延迟
- **最大延迟**：最慢的请求延迟

### 可靠性指标

- **成功率**：成功响应的百分比
- **失败数**：失败的请求数

## 性能优化建议

### 1. 选择合适的传输协议

- **INPROC**：进程内通信，性能最优
- **IPC**：本地进程间通信，性能优于 TCP
- **UDP**：高吞吐、可容忍丢包的场景
- **TCP**：需要可靠传输的场景

### 2. 调整批处理大小

- **小批处理**（< 50）：低延迟，低吞吐
- **中等批处理**（50-100）：平衡延迟和吞吐
- **大批处理**（> 100）：高吞吐，高延迟

### 3. 启用低延迟模式

```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -l
```

### 4. 使用适当的并发级别

- 单线程：简单场景
- 多线程：提高吞吐量
- 多进程：测试隔离性

## 已知限制

1. **线程数限制**：最多支持 10 个线程（MAX_THREADS）
2. **客户端数限制**：每个线程最多 100 个客户端（MAX_CLIENTS）
3. **进程数限制**：最多支持 32 个进程（MAX_PROCESSES）

## 故障排查

### 连接失败

```bash
# 检查端口是否被占用
lsof -i :5555

# 检查服务器是否运行
ps aux | grep benchmark
```

### 性能异常

```bash
# 使用 Release 模式编译
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build

# 检查系统资源
top
vmstat
```

## 参考文档

- [性能测试报告](/en/performance-report)
- [设计哲学](/en/guide/design-philosophy)
- [单线程模型](/en/guide/single-thread-model)