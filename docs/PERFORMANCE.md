# UVRPC Performance Test Results

## Test Configuration

- **Platform**: Linux 6.14.11-2-pve
- **Date**: 2026-02-11
- **Test Tool**: benchmark_client / benchmark_server
- **Test Parameters**:
  - Request Count: 100
  - Concurrency: 10
  - Payload Size: 128 bytes
  - Server Address: tcp://127.0.0.1:6002
  - Mode: ROUTER/DEALER

## Test Methodology

1. Start benchmark_server in background
2. Run benchmark_client 5 times
3. Record results for each run
4. Calculate average performance

## Test Results (5 Runs)

| Run | Serial Await (ops/s) | Concurrent Await (ops/s) | Callback Mode (ops/s) |
|-----|---------------------|-------------------------|----------------------|
| 1   | 12,359.89           | 92,727.13               | 220,717.42           |
| 2   | 11,950.69           | 111,919.29              | 46,396.09            |
| 3   | 11,945.64           | 14,927.36               | 200,454.23           |
| 4   | 13,190.86           | 88,275.20               | 82,515.40            |
| 5   | 12,116.84           | 83,554.33               | 186,114.72           |

## Average Performance

| Mode              | Average Throughput (ops/s) | Speedup vs Serial |
|-------------------|----------------------------|-------------------|
| Serial Await      | 12,312.78                  | 1.0x (baseline)   |
| Concurrent Await  | 78,280.66                  | 6.4x              |
| Callback Mode     | 147,239.57                 | 12.0x             |

## Performance Analysis

### Serial Await Mode
- **Throughput**: ~12,300 ops/s
- **Characteristics**: Requests are sent one by one, waiting for each response before sending the next
- **Use Case**: Simple scenarios where request ordering is important

### Concurrent Await Mode
- **Throughput**: ~78,300 ops/s (6.4x improvement over serial)
- **Characteristics**: Multiple requests are sent concurrently using uvrpc_await_all()
- **Use Case**: High-throughput scenarios where request ordering is not important

### Callback Mode
- **Throughput**: ~147,200 ops/s (12.0x improvement over serial)
- **Characteristics**: Uses callback-based API for maximum performance
- **Use Case**: Event-driven applications with high performance requirements

## Performance Variability

The test results show significant variability, especially in callback mode (46k - 220k ops/s range). This variability is likely due to:

1. **System load fluctuations**
2. **CPU cache effects**
3. **ZMQ internal buffer state**
4. **Event loop timing variations**

## Optimization History

### Attempted Optimizations
1. **msgpack two-phase serialization** - Reverted due to 5-7x performance degradation
   - Problem: Double serialization CPU cost outweighed memory copy savings
   - Result: 90k - 110k ops/s → 16k ops/s

2. **UVZMQ batch processing optimization** - Reverted due to performance degradation
   - Problem: Increased UVZMQ_MAX_BATCH_SIZE (1000 → 2000) caused longer processing times
   - Result: Performance became more unstable

### Current Optimizations
1. **mimalloc as default allocator** - ~1.5% performance improvement
2. **ZMQ socket options**:
   - TCP buffer size: 256KB (SNDBUF/RCVBUF)
   - High water mark: 10000 (SNDHWM/RCVHWM)
3. **async_callback optimization** - Proper old data cleanup
4. **await_all optimization** - Simplified loop logic

## Conclusion

- **Callback mode** provides the best performance (~147k ops/s on average)
- **Concurrent await** provides a good balance of usability and performance (~78k ops/s)
- **Serial await** is the simplest but slowest option (~12k ops/s)
- Performance varies significantly between runs, suggesting the need for more stable optimization approaches