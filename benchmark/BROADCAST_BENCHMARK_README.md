# UVRPC Broadcast Benchmark

## Overview

广播模式性能测试程序，用于测试发布者-订阅者模式的性能。

## 程序

### broadcast_benchmark
统一的广播性能测试程序，支持发布者和订阅者两种模式。

## 使用方法

### Publisher 模式

发布者模式用于发送消息，测试发布性能。

**基本用法：**
```bash
./dist/bin/broadcast_benchmark publisher [options]
```

**选项：**
- `-a <address>` - 地址 (默认: udp://127.0.0.1:6000)
- `-t <topic>` - 主题 (默认: benchmark_topic)
- `-d <duration>` - 测试持续时间，单位毫秒 (默认: 1000)
- `-s <size>` - 消息大小，单位字节 (默认: 100)
- `-b <batch>` - 批量大小 (默认: 10)
- `-h` - 显示帮助

**示例：**

**UDP 发布者测试（默认）：**
```bash
./dist/bin/broadcast_benchmark publisher -a udp://127.0.0.1:6000 -d 5000
```

**TCP 发布者测试：**
```bash
./dist/bin/broadcast_benchmark publisher -a tcp://127.0.0.1:6001 -d 5000
```

**IPC 发布者测试：**
```bash
./dist/bin/broadcast_benchmark publisher -a ipc:///tmp/benchmark.sock -d 5000
```

**INPROC 发布者测试：**
```bash
./dist/bin/broadcast_benchmark publisher -a inproc://benchmark -d 5000
```

**大消息测试：**
```bash
./dist/bin/broadcast_benchmark publisher -a udp://127.0.0.1:6000 -d 3000 -s 1024 -b 50
```

### Subscriber 模式

订阅者模式用于接收消息，测试订阅性能。

**基本用法：**
```bash
./dist/bin/broadcast_benchmark subscriber [options]
```

**选项：**
- `-a <address>` - 地址 (默认: udp://127.0.0.1:6000)
- `-t <topic>` - 主题 (默认: benchmark_topic)
- `-d <duration>` - 测试持续时间，单位毫秒 (默认: 1000)
- `-h` - 显示帮助

**示例：**

**UDP 订阅者测试（默认）：**
```bash
./dist/bin/broadcast_benchmark subscriber -a udp://127.0.0.1:6000 -d 5000
```

**TCP 订阅者测试：**
```bash
./dist/bin/broadcast_benchmark subscriber -a tcp://127.0.0.1:6001 -d 5000
```

## 传输层支持

广播模式支持以下 4 种传输层：

| 传输层 | 地址格式 | 特点 |
|--------|---------|------|
| **UDP** | `udp://127.0.0.1:6000` | 无连接，高性能，推荐用于广播 |
| **TCP** | `tcp://127.0.0.1:6001` | 可靠传输，保证送达 |
| **IPC** | `ipc:///tmp/benchmark.sock` | Unix Domain Socket，高性能本地通信 |
| **INPROC** | `inproc://benchmark` | 进程内通信，最快速度 |

## 完整测试流程

### 1. 启动订阅者
```bash
./dist/bin/broadcast_benchmark subscriber -a udp://127.0.0.1:6000 -d 10000
```

### 2. 启动发布者（在另一个终端）
```bash
./dist/bin/broadcast_benchmark publisher -a udp://127.0.0.1:6000 -d 10000 -s 100 -b 20
```

### 3. 查看结果

**发布者结果：**
```
=== Publisher Results ===
Duration: 10.000 seconds
Messages published: 123456
Bytes sent: 12345600
Throughput: 12345.60 msg/s
Bandwidth: 1205.62 KB/s
```

**订阅者结果：**
```
=== Subscriber Results ===
Messages received: 123450
Bytes received: 12345000
Average message size: 100.00 bytes
```

## 性能指标

### 发布者指标
- **Duration** - 测试持续时间（秒）
- **Messages published** - 发布的消息总数
- **Bytes sent** - 发送的字节总数
- **Throughput** - 吞吐量（消息/秒）
- **Bandwidth** - 带宽（KB/s）

### 订阅者指标
- **Messages received** - 接收的消息总数
- **Bytes received** - 接收的字节总数
- **Average message size** - 平均消息大小（字节）

## 与 Server-Client 模式的区别

| 特性 | Server-Client | Broadcast |
|------|--------------|-----------|
| **通信模式** | 请求-响应 | 发布-订阅 |
| **测试程序** | perf_benchmark | broadcast_benchmark |
| **默认地址** | tcp://127.0.0.1:5555 | udp://127.0.0.1:6000 |
| **测试指标** | 请求/响应、延迟 | 发布/接收、吞吐量 |
| **适用场景** | RPC 调用 | 消息广播、事件通知 |
| **推荐传输** | TCP | UDP |

## 性能优化建议

1. **使用 UDP 传输**：广播场景推荐使用 UDP，性能最佳
2. **调整批量大小**：增加批量大小可以提升吞吐量
3. **消息大小**：较小的消息大小通常能获得更高的吞吐量
4. **本地通信**：使用 IPC 或 INPROC 可以获得最佳性能

## 常见问题

**Q: 为什么 UDP 是广播模式的推荐传输？**  
A: UDP 是无连接传输，天然适合广播/多播场景，性能开销最小。

**Q: 如何测试多订阅者场景？**  
A: 启动多个订阅者进程，然后启动一个发布者。

**Q: 订阅者接收的消息数少于发布者发送的消息数？**  
A: 正常现象，UDP 可能丢包。可以使用 TCP 测试可靠传输。

**Q: 如何测试最大吞吐量？**  
A: 增加批量大小（`-b`参数）和测试持续时间（`-d`参数）。

## 编译

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make broadcast_benchmark
```

## 特性

- **无阻塞等待**：使用事件循环等待，不使用 sleep
- **原子计数器**：线程安全的统计计数
- **批量发布**：支持批量发送提升吞吐量
- **多传输支持**：支持 UDP、TCP、IPC、INPROC
- **可配置参数**：地址、主题、持续时间、消息大小、批量大小均可配置