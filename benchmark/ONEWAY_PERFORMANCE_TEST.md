# UVRPC Oneway Performance Test

## Test Overview

This document contains performance test results for UVRPC oneway methods compared to regular RPC methods.

## Test Environment

- **System**: Linux 6.14.11-2-pve
- **CPU**: [To be filled]
- **Memory**: [To be filled]
- **UVRPC Version**: 0.1.0

## Test Configuration

- **Requests per client**: 100,000
- **Number of clients**: 10
- **Total operations**: 1,000,000
- **Transport**: TCP
- **Optimization level**: -O2

## Test Methods

### 1. Oneway Method (`Log`)
- Characteristics: No response expected
- Internal implementation: `uvrpc_client_call_oneway()`
- Use case: Logging, notifications, events

### 2. Regular RPC Method (`Add`)
- Characteristics: Requires response
- Internal implementation: `uvrpc_client_call()`
- Use case: Standard request-response operations

## Expected Results

### Oneway Method Advantages:

1. **Higher Throughput**
   - No response wait time
   - Reduced round-trip latency
   - Expected: 2-5x higher throughput than regular RPC

2. **Lower Latency**
   - Single-direction communication
   - No callback overhead
   - Expected: 50-70% lower latency

3. **Better Scalability**
   - Fire-and-forget pattern
   - Reduced resource usage
   - Higher concurrent operations

### Performance Metrics to Track:

- **Throughput**: Operations per second (ops/sec)
- **Latency**: Time per operation (µs)
- **CPU Usage**: Client and server CPU
- **Memory Usage**: Memory consumption
- **Success Rate**: Percentage of successful operations

## How to Run Tests

```bash
# Run basic oneway performance test
./scripts/test_oneway_perf.sh

# Run comparison test
./scripts/test_oneway_vs_regular.sh
```

## Test Results

### Test Run 1: [Date]

```
[Results to be filled]
```

**Summary:**
- Oneway throughput: X ops/sec
- Regular RPC throughput: Y ops/sec
- Performance improvement: Z%

### Test Run 2: [Date]

```
[Results to be filled]
```

## Analysis

### Oneway Method Performance Characteristics

- **Best Use Cases**:
  - High-frequency logging
  - Event notifications
  - Metrics collection
  - Status updates

- **Limitations**:
  - No error feedback
  - No result confirmation
  - Potential message loss if queue full

### Recommendations

1. Use oneway methods when:
   - Response is not required
   - High throughput is critical
   - Occasional message loss is acceptable

2. Use regular RPC when:
   - Response confirmation is needed
   - Error handling is critical
   - Transactional operations

## Future Improvements

- Add UDP transport comparison
- Test with different payload sizes
- Measure memory usage precisely
- Add concurrent stress tests
- Compare with other RPC frameworks

---
*Last updated: 2026-02-21*
