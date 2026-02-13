# UVRPC Benchmark

性能测试程序，用于测试 UVRPC 在各种配置下的性能。

## 编译

benchmark 必须使用 Release 模式编译（-O2 优化，NDEBUG 定义）。

### 编译步骤

```bash
cd benchmark
cmake .
make
```

## 配置

benchmark 有独立的 CMake 配置，确保以下设置：

- **Build Type**: 强制使用 Release 模式
- **优化级别**: -O2（可通过 CMake 调整为 -O3）
- **Debug 定义**: NDEBUG 已定义（禁用断言）
- **分配器**: 自动检测 mimalloc，如可用则使用

### 编译选项

```bash
# 查看当前配置
cmake -L .

# 强制 Release 模式（默认已配置）
cmake -DCMAKE_BUILD_TYPE=Release .
```

## 运行

```bash
cd benchmark
./uvrpc_benchmark [options]
```

## 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-t, --transport` | 传输方式 (tcp/udp/ipc/inproc) | inproc |
| `-p, --port` | 端口号 | 5555 |
| `-a, --address` | 地址 | 127.0.0.1 |
| `-s, --size` | 负载大小（字节） | 1024 |
| `-c, --clients` | 客户端数量 | 1 |
| `-n, --concurrency` | 单客户端并发数 | 10 |
| `-r, --requests` | 每客户端请求总数 | 10000 |
| `-T, --timeout` | 超时时间（毫秒） | 5000 |
| `-w, --warmup` | 预热请求数 | 100 |
| `-v, --verbose` | 详细输出 | - |
| `-h, --help` | 帮助信息 | - |

## 架构

- **独立线程**: 每个客户端在独立 pthread 中运行
- **独立 Loop**: 每个客户端有独立的 libuv 事件循环
- **零同步**: 不使用任何锁或临界区
- **独立统计**: 每个客户端维护独立统计信息

## 示例

```bash
# 基础测试
./uvrpc_benchmark -s 128 -c 1 -n 10 -r 100

# 高并发测试
./uvrpc_benchmark -t inproc -s 1024 -c 10 -n 100 -r 10000

# TCP 传输测试
./uvrpc_benchmark -t tcp -a 127.0.0.1 -p 5555 -s 2048 -c 5 -n 50 -r 1000

# 大负载测试
./uvrpc_benchmark -t inproc -s 10240 -c 4 -n 20 -r 5000
```

## 输出说明

- **Throughput**: 吞吐量（请求/秒）
- **Avg latency**: 平均延迟
- **Min/Max latency**: 最小/最大延迟
- **P50/P95/P99**: 50%/95%/99% 延迟百分位数
- **Success/Failed/Timeout**: 成功/失败/超时请求数