# Performance Testing

UVRPC provides a unified performance testing tool `benchmark` that supports various test scenarios and transport protocols.

## Benchmark Tool

The `benchmark` program is a unified performance testing tool with the following features:

- **CS Mode (Client-Server)**: Test request-response mode performance
- **Broadcast Mode (Publisher-Subscriber)**: Test publish-subscribe mode performance
- **Multi-thread/Multi-process Testing**: Support concurrent testing
- **Latency Testing**: Measure request-response latency
- **Multiple Transport Protocols**: TCP, UDP, IPC, INPROC

## Usage

### Start Server (CS Mode)

```bash
# Basic usage
./dist/bin/benchmark --server

# Specify address
./dist/bin/benchmark --server -a tcp://127.0.0.1:5555

# Set auto-shutdown timeout (milliseconds)
./dist/bin/benchmark --server --server-timeout 5000
```

### Run Client Test (CS Mode)

```bash
# Single client test
./dist/bin/benchmark -a tcp://127.0.0.1:5555

# Specify test duration (milliseconds)
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000

# Specify batch size
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -b 100

# Multi-client test
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -c 10

# Multi-thread test
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 5 -c 2

# Low latency mode
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -l

# Latency test
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --latency

# Multi-process test
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --fork -t 3 -c 2
```

### Start Publisher (Broadcast Mode)

```bash
# Basic usage
./dist/bin/benchmark --publisher

# Specify address
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000

# Multi-publisher test
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 3

# Multi-thread multi-publisher
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -t 3 -p 2

# Specify batch size and duration
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -b 20 -d 5000
```

### Start Subscriber (Broadcast Mode)

```bash
# Basic usage
./dist/bin/benchmark --subscriber

# Specify address
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000

# Multi-subscriber test
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -s 5

# Multi-thread multi-subscriber
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -t 3 -s 2
```

## Parameters

### Mode Parameters

- `--server`: Server mode (CS mode)
- `--publisher`: Publisher mode (broadcast mode)
- `--subscriber`: Subscriber mode (broadcast mode)

### Common Parameters

- `-a <address>`: Server/publisher address (default: tcp://127.0.0.1:5555)
- `-t <threads>`: Number of threads/processes (default: 1)
- `-b <concurrency>`: Batch size (default: 100)
- `-d <duration>`: Test duration in milliseconds (default: 1000)
- `-l`: Enable low latency mode (default: high throughput)
- `--latency`: Run latency test (ignores -t and -c)
- `--fork`: Use fork mode instead of threads (multi-process testing)
- `-h`: Show help information

### CS Mode Parameters

- `-c <clients>`: Clients per thread/process (default: 1)

### Broadcast Mode Parameters

- `-p <publishers>`: Publishers per thread/process (broadcast mode, default: 1)
- `-s <subscribers>`: Subscribers per thread/process (broadcast mode, default: 1)

### Server Parameters

- `--server-timeout <ms>`: Server auto-shutdown timeout (default: 0, no timeout)

## Test Scenarios

### 1. Basic Throughput Test

```bash
# Start server
./dist/bin/benchmark --server -a tcp://127.0.0.1:5555

# Run client in another terminal
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000 -b 100
```

### 2. Multi-client Concurrent Test

```bash
# 10 concurrent clients
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -c 10 -d 2000
```

### 3. Multi-thread Test

```bash
# 5 threads, 2 clients per thread
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 5 -c 2 -d 2000
```

### 4. Latency Test

```bash
# Test request-response latency
./dist/bin/benchmark -a tcp://127.0.0.1:5555 --latency
```

### 5. Broadcast Mode Test

```bash
# Start publisher
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 3 -b 20 -d 5000

# Start subscriber in another terminal
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -s 5 -d 5000
```

### 6. Multi-transport Test

```bash
# TCP test
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -d 2000

# IPC test
./dist/bin/benchmark -a ipc:///tmp/uvrpc_test.sock -d 2000

# UDP test
./dist/bin/benchmark -a udp://127.0.0.1:5556 -d 2000

# INPROC test
./dist/bin/benchmark -a inproc://test -d 2000
```

## Performance Metrics

### Throughput Metrics

- **Ops/s**: Operations per second (CS mode)
- **Msgs/s**: Messages per second (broadcast mode)
- **Bandwidth**: Data transfer rate (MB/s)

### Latency Metrics

- **Average Latency**: Average response time for all requests
- **P50 Latency**: Median latency
- **P95 Latency**: 95th percentile latency
- **P99 Latency**: 99th percentile latency
- **Max Latency**: Slowest request latency

### Reliability Metrics

- **Success Rate**: Percentage of successful responses
- **Failures**: Number of failed requests

## Performance Optimization Tips

### 1. Choose the Right Transport Protocol

- **INPROC**: In-process communication, best performance
- **IPC**: Local inter-process communication, better than TCP
- **UDP**: High throughput, tolerates packet loss
- **TCP**: Reliable transmission

### 2. Adjust Batch Size

- **Small batch** (< 50): Low latency, low throughput
- **Medium batch** (50-100): Balanced latency and throughput
- **Large batch** (> 100): High throughput, high latency

### 3. Enable Low Latency Mode

```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -l
```

### 4. Use Appropriate Concurrency Level

- Single thread: Simple scenarios
- Multi-thread: Higher throughput
- Multi-process: Test isolation

## Known Limitations

1. **Thread limit**: Maximum 10 threads (MAX_THREADS)
2. **Client limit**: Maximum 100 clients per thread (MAX_CLIENTS)
3. **Process limit**: Maximum 32 processes (MAX_PROCESSES)

## Troubleshooting

### Connection Failure

```bash
# Check if port is in use
lsof -i :5555

# Check if server is running
ps aux | grep benchmark
```

### Performance Issues

```bash
# Build in Release mode
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build

# Check system resources
top
vmstat
```

## Related Documentation

- [Performance Report](/en/performance-report)
- [Design Philosophy](/en/guide/design-philosophy)
- [Single Thread Model](/en/guide/single-thread-model)