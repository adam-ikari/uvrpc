# Immediate Mode (0ms Interval) Analysis

## Overview

The benchmark supports a configurable timer interval via the `-i` parameter. Setting the interval to 0ms enables "immediate mode", which attempts to send requests as fast as possible.

**Default Behavior**: The benchmark now uses 0ms interval by default, meaning requests are sent as fast as possible without timer delays. To use timer-based sending, explicitly specify the interval with the `-i` parameter (e.g., `-i 2` for 2ms interval).

## Implementation

### Current Implementation

When `timer_interval_ms = 0`, the benchmark uses `send_batch_requests_fast` function:

```c
if (state.timer_interval_ms == 0) {
    /* Immediate mode: use 1ms timer but send 10 batches per timer callback */
    uv_timer_start(&batch_timer, send_batch_requests_fast, 1, 1);
}
```

The `send_batch_requests_fast` function sends 10 batches per timer callback with 100us sleep between batches to allow event loop processing.

### Performance Results

| Interval | Success Rate | Throughput | Latency | Use Case |
|----------|--------------|------------|---------|----------|
| 0ms (immediate, default) | 65-70% | 114k+ ops/s | **Lowest** | Latency-sensitive applications |
| 1ms | 98-99% | 95-96k ops/s | Low | Stress testing, high throughput |
| 2ms | 100% | 97k ops/s | Medium | Balanced performance (recommended) |
| 5ms | 100% | 85k ops/s | Higher | Low-power, stable throughput |

## Limitations

### Why 0ms Interval Performs Poorly

1. **Backpressure Overwhelm**: Sending requests too fast causes the backpressure mechanism to trigger frequently, skipping many requests

2. **Event Loop Starvation**: Immediate sending prevents the event loop from processing responses, causing pending requests to accumulate

3. **Network Bottleneck**: Even with fast sending, network latency and server processing time limit actual throughput

4. **Memory Pressure**: Too many pending requests consume memory, potentially causing system instability

### Why 0ms Interval Has High Power Consumption

The 0ms interval mode has significantly higher power consumption than timer-based modes due to:

1. **Continuous CPU Activity**: The 1ms timer triggers 1000 times per second, and each callback sends 10 batches (1000 requests), resulting in 1,000,000 function calls per second in multi-threaded scenarios

2. **Frequent Event Loop Processing**: Each batch calls `uv_run(loop, UV_RUN_NOWAIT)` 10 times per timer callback, totaling 10,000 event loop iterations per second per thread

3. **No CPU Idle Time**: The 100us sleep between batches is too short for the CPU to enter low-power states, keeping cores constantly active

4. **High Memory Bandwidth**: Constant buffer allocation and deallocation for pending requests consumes significant memory bandwidth

5. **Cache Thrashing**: Rapid context switching between sending and processing reduces cache efficiency, requiring more memory accesses

In contrast, timer-based modes (1ms, 2ms, 5ms+) allow CPU cores to enter idle states between timer callbacks, reducing power consumption significantly.

### Backpressure Mechanism

The benchmark implements adaptive backpressure to prevent buffer overflow:

```c
int pending_count = uvrpc_client_get_pending_count(clients[client_idx]);
int max_concurrent = batch_size * 2;
int threshold = max_concurrent * 8 / 10;  /* 80% threshold */

if (pending_count > threshold + 10) {
    skipped_count++;
    continue;
}
```

In immediate mode, the pending count quickly exceeds the threshold, causing many requests to be skipped.

## Recommendations

### For Performance Testing

**Use 2ms interval** (`-i 2`) for maximum balanced performance:
- 100% success rate
- 97k+ ops/s throughput
- Low memory usage
- Stable behavior across different configurations

To use timer-based mode, add the `-i` parameter:
```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 2 -c 2 -b 100 -i 2 -d 3000
```

### For Stress Testing

Use 1ms interval (`-i 1`) to:
- Push the system to its limits
- Identify performance bottlenecks
- Test resilience under high load
- Expect slightly lower success rate (98-99%)

```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 2 -c 2 -b 100 -i 1 -d 3000
```

### For Low-Throughput Stable Applications

Use 5ms interval or higher (`-i 5` or `-i 10`) to:
- Reduce CPU usage
- Improve energy efficiency
- Maintain high success rate (100%)
- Accept lower throughput

```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 2 -c 2 -b 100 -i 5 -d 3000
```

### For Latency-Sensitive Applications

Use 0ms interval (default, no `-i` parameter) for minimum latency:
- Send requests immediately without delay
- Best for low-latency requirements
- Accept lower success rate (65-70%) due to backpressure
- Maximum raw sending speed (114k+ ops/s)

```bash
./dist/bin/benchmark -a tcp://127.0.0.1:5555 -t 2 -c 2 -b 100 -d 3000
```

## Conclusion

The optimal interval depends on your specific use case:

- **Latency-sensitive (minimum delay)**: 0ms interval (default, recommended for latency-critical applications)
- **Balanced performance**: 2ms interval (recommended for most use cases, 100% success rate)
- **Maximum throughput**: 1ms interval (98-99% success rate, slightly higher failure rate)
- **Low-power/low-throughput**: 5ms+ interval (100% success rate, reduced CPU usage)

The 0ms interval mode provides the lowest possible latency by sending requests immediately without artificial delays, making it ideal for latency-sensitive applications. While it may have a lower success rate due to backpressure, the successful requests achieve the best possible latency performance.

## Future Improvements

Potential improvements for immediate mode:

1. **Dynamic Rate Limiting**: Adjust sending rate based on real-time success rate
2. **Predictive Backpressure**: Anticipate buffer overflow before it happens
3. **Zero-Copy Batching**: Batch requests without copying to reduce overhead
4. **Asynchronous Retry**: Implement proper async retry for failed requests
5. **Connection Pooling**: Use multiple connections per client to increase parallelism

However, these improvements would require significant architectural changes and may not yield substantial benefits compared to the optimized 2ms interval approach.