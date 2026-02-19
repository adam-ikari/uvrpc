# UVRPC Performance Tools Guide

## Overview

UVRPC provides several tools for automated performance testing, monitoring, and result analysis. This guide explains how to use each tool.

## Available Tools

### 1. Automated Performance Test Runner

**Location**: `benchmark/run_automated_perf_tests.sh`

**Purpose**: Automatically run comprehensive performance tests across all configurations and generate detailed reports.

**Usage**:
```bash
cd benchmark
./run_automated_perf_tests.sh
```

**What it does**:
- Tests all transport layers (TCP, UDP, IPC)
- Tests multiple thread configurations (1, 2, 4 threads)
- Tests multiple client configurations (1, 2, 5 clients)
- Tests multiple timer intervals (1ms, 2ms, 5ms)
- Generates comprehensive markdown report

**Output**: `benchmark/results/automated_perf_report_<timestamp>.md`

**Report Contents**:
- Executive summary with best performers
- Detailed results for each configuration
- Transport comparison tables
- Timer interval analysis
- Scalability analysis
- Recommendations

---

### 2. Quick Performance Test

**Location**: `tools/quick_perf.sh`

**Purpose**: Quick performance test with default optimal configuration.

**Usage**:
```bash
# Default configuration (TCP, 2 threads, 2 clients, 2ms interval)
./tools/quick_perf.sh

# Custom configuration
./tools/quick_perf.sh tcp://127.0.0.1:5000 4 5 100 2 3000

# UDP with custom config
./tools/quick_perf.sh udp://127.0.0.1:6000 2 2 100 2 3000
```

**Parameters**:
1. `address` - Server address (default: tcp://127.0.0.1:5000)
2. `threads` - Number of threads (default: 2)
3. `clients` - Clients per thread (default: 2)
4. `concurrency` - Batch size/concurrency (default: 100)
5. `interval` - Timer interval in ms (default: 2)
6. `duration` - Test duration in ms (default: 3000)

**Output**:
- Console output with test results
- Exit code 0 on success, non-zero on failure

---

### 3. Performance Monitor

**Location**: `tools/perf_monitor.sh`

**Purpose**: Real-time performance monitoring with live stats.

**Usage**:
```bash
# Monitor TCP with default config
./tools/perf_monitor.sh

# Monitor UDP with custom interval and duration
./tools/perf_monitor.sh udp://127.0.0.1:6000 2 10000

# Monitor IPC for 30 seconds
./tools/perf_monitor.sh ipc:///tmp/uvrpc.sock 5 30000
```

**Parameters**:
1. `address` - Server address (default: tcp://127.0.0.1:5000)
2. `interval` - Timer interval in ms (default: 2)
3. `duration` - Monitor duration in ms (default: 10000)

**Features**:
- Real-time stats display (updated every second)
- Shows requests, responses, success rate, throughput
- Color-coded output for easy reading
- Summary at end of monitoring

**Output Example**:
```
[10:30:45] Req: 145,300 Resp: 145,282 Success: 100% Throughput: 96,853 ops/s
```

---

### 4. Performance Results Comparison

**Location**: `tools/compare_perf_results.py`

**Purpose**: Compare multiple performance test reports and generate comparison analysis.

**Usage**:
```bash
# Compare two reports
./tools/compare_perf_results.py \
    benchmark/results/report1.md \
    benchmark/results/report2.md \
    -o benchmark/results/comparison.md

# Compare multiple reports
./tools/compare_perf_results.py \
    benchmark/results/*.md \
    -o benchmark/results/full_comparison.md
```

**Parameters**:
- `reports` - One or more markdown report files (required)
- `-o` - Output file (default: stdout)

**Output**:
- Comprehensive comparison report
- Best performers by category
- Transport comparison tables
- Timer interval analysis
- Scalability analysis
- Recommendations

---

## Workflow Examples

### Example 1: Full Performance Analysis

```bash
# Step 1: Run automated tests
cd benchmark
./run_automated_perf_tests.sh

# Step 2: Review results
cat benchmark/results/automated_perf_report_*.md

# Step 3: Compare with previous results
./tools/compare_perf_results.py \
    benchmark/results/automated_perf_report_*.md \
    -o benchmark/results/comparison.md

# Step 4: Monitor performance in real-time
cd ..
./tools/perf_monitor.sh tcp://127.0.0.1:5000 2 30000
```

### Example 2: Quick Performance Check

```bash
# Quick test with default optimal config
./tools/quick_perf.sh

# Test UDP transport
./tools/quick_perf.sh udp://127.0.0.1:6000 2 2 100 2 3000

# Test with higher concurrency
./tools/quick_perf.sh tcp://127.0.0.1:5000 4 5 200 2 5000
```

### Example 3: Performance Regression Testing

```bash
# Run baseline test
./tools/quick_perf.sh > baseline.txt

# Make changes to code
# ...

# Run regression test
./tools/quick_perf.sh > regression.txt

# Compare results
diff baseline.txt regression.txt

# Or use comparison tool
./tools/compare_perf_results.py \
    baseline_report.md \
    regression_report.md
```

### Example 4: Continuous Performance Monitoring

```bash
# Monitor for 5 minutes
./tools/perf_monitor.sh tcp://127.0.0.1:5000 2 300000

# Monitor in background with logging
./tools/perf_monitor.sh tcp://127.0.0.1:5000 2 60000 > monitor.log &
MONITOR_PID=$!

# Do other work...

# Stop monitoring
kill $MONITOR_PID
cat monitor.log
```

---

## Best Practices

### 1. Choosing Timer Interval

| Scenario | Recommended Interval | Reason |
|----------|---------------------|--------|
| Production | 2ms | Best balance (100% success, 97k ops/s) |
| Maximum Throughput | 1ms | Accept 13% failure rate for +42% throughput |
| Maximum Stability | 5ms | Accept 60% throughput loss for 100% stability |
| Development | 2ms | Good balance for testing |

### 2. Choosing Thread/Client Configuration

| Scenario | Threads | Clients | Reason |
|----------|---------|---------|--------|
| Low Load | 1 | 1 | Single-threaded, 100% success |
| Medium Load | 2 | 2 | Balanced, good scalability |
| High Load | 4 | 2 | Multi-threaded, good scalability |
| Many Clients | 2 | 5 | Multi-client, good scalability |

### 3. Interpreting Results

**Success Rate**:
- 100%: Excellent - System is stable
- 95-99%: Good - Minor issues
- 90-95%: Acceptable - Consider tuning
- <90%: Poor - Needs investigation

**Throughput**:
- >100,000 ops/s: Excellent
- 50,000-100,000 ops/s: Good
- 20,000-50,000 ops/s: Acceptable
- <20,000 ops/s: Poor

**Memory**:
- <5 MB: Excellent
- 5-10 MB: Good
- 10-20 MB: Acceptable
- >20 MB: Poor (memory leak or bloat)

---

## Troubleshooting

### Issue: Server fails to start

**Symptoms**:
```
Server failed to start
[ERROR] Bind failed: address already in use
```

**Solution**:
```bash
# Kill existing processes
pkill -9 benchmark

# Or use different port
./tools/quick_perf.sh tcp://127.0.0.1:5001
```

### Issue: Test hangs or times out

**Symptoms**:
- Test doesn't complete
- Timeout error after 60 seconds

**Solution**:
```bash
# Check if processes are running
ps aux | grep benchmark

# Kill stuck processes
pkill -9 benchmark

# Reduce test duration
./tools/quick_perf.sh tcp://127.0.0.1:5000 2 2 100 2 1000
```

### Issue: Low success rate

**Symptoms**:
- Success rate < 90%
- Many failures

**Solution**:
1. Increase timer interval (1ms → 2ms → 5ms)
2. Reduce concurrency (100 → 50)
3. Check system resources (CPU, memory)
4. Reduce thread count (4 → 2)

### Issue: High memory usage

**Symptoms**:
- Memory > 20 MB
- Memory leak suspected

**Solution**:
1. Check for memory leaks using `valgrind`
2. Reduce concurrency
3. Check for connection leaks
4. Review recent code changes

---

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: Performance Test

on: [push, pull_request]

jobs:
  perf-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build UVRPC
        run: |
          make -j$(nproc)
      
      - name: Run Automated Performance Tests
        run: |
          cd benchmark
          ./run_automated_perf_tests.sh
      
      - name: Upload Performance Report
        uses: actions/upload-artifact@v2
        with:
          name: performance-report
          path: benchmark/results/automated_perf_report_*.md
      
      - name: Compare with Baseline
        run: |
          ./tools/compare_perf_results.py \
            benchmark/results/automated_perf_report_*.md \
            -o benchmark/results/comparison.md
```

---

## Advanced Usage

### Custom Test Suite

Create a custom test script:

```bash
#!/bin/bash
# Custom performance test suite

# Test different configurations
CONFIGS=(
    "tcp://127.0.0.1:5000 2 2 100 1 3000"
    "tcp://127.0.0.1:5001 2 2 100 2 3000"
    "tcp://127.0.0.1:5002 2 2 100 5 3000"
    "udp://127.0.0.1:6000 2 2 100 2 3000"
)

for config in "${CONFIGS[@]}"; do
    echo "Testing: $config"
    ./tools/quick_perf.sh $config > result_$config.txt
done
```

### Performance Profiling

Use with performance profiling tools:

```bash
# Profile with perf
perf record -g ./tools/quick_perf.sh
perf report

# Profile with valgrind
valgrind --tool=massif ./tools/quick_perf.sh
```

### Automated Regression Testing

```bash
#!/bin/bash
# Automated regression testing script

# Save baseline
./tools/quick_perf.sh > baseline.txt

# Run tests
make test

# Run regression test
./tools/quick_perf.sh > current.txt

# Compare
if diff baseline.txt current.txt > /dev/null; then
    echo "No regression detected"
else
    echo "Regression detected!"
    exit 1
fi
```

---

## Summary

| Tool | Purpose | Usage | Output |
|------|---------|-------|--------|
| `run_automated_perf_tests.sh` | Comprehensive testing | `./run_automated_perf_tests.sh` | Markdown report |
| `quick_perf.sh` | Quick test | `./quick_perf.sh [args]` | Console output |
| `perf_monitor.sh` | Real-time monitoring | `./perf_monitor.sh [args]` | Live stats + summary |
| `compare_perf_results.py` | Result comparison | `./compare_perf_results.py *.md` | Comparison report |

---

**Last Updated**: 2026-02-19  
**Version**: 1.0  
**Author**: UVRPC Performance Team