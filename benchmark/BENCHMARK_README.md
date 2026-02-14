# UVRPC Benchmark Programs

## Overview

This directory contains performance testing programs for UVRPC.

## Programs

### 1. perf_server
Performance test server using generated DSL API.

**Usage:**
```bash
./dist/bin/perf_server [address]
```

**Default:** tcp://127.0.0.1:5555

### 2. perf_benchmark
Unified benchmark client supporting multiple test modes.

**Usage:**
```bash
./dist/bin/perf_benchmark [options]
```

**Options:**
- `-a <address>` - Server address (default: 127.0.0.1:5555)
- `-i <iterations>` - Total iterations (default: 10000)
- `-t <threads>` - Number of threads (default: 1)
- `-c <clients>` - Clients per thread (default: 1)
- `-b <concurrency>` - Batch size (default: 100)
- `-l` - Enable low latency mode (default: high throughput)
- `--latency` - Run latency test (ignores -t and -c)
- `-h` - Show help

**Examples:**

**Single client throughput test:**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -i 10000 -b 100
```

**Multi-client throughput test (10 clients):**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -i 10000 -c 10 -b 100
```

**Multi-thread test (5 threads, 2 clients each):**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -t 5 -c 2 -b 50
```

**Multi-thread test with low latency mode:**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -t 5 -c 2 -b 50 -l
```

**Latency test:**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -i 1000 --latency
```

**Latency test with low latency mode:**
```bash
./dist/bin/perf_benchmark -a 127.0.0.1:5555 -i 1000 --latency -l
```

## Building

```bash
cd build
cmake ..
make perf_server perf_benchmark
```

## Features

- **No sleep**: All benchmark programs use event loop waiting instead of sleep
- **Generated DSL API**: Uses FlatBuffers DSL generated code for type-safe RPC calls
- **Configurable concurrency**: Accepts concurrency parameters from command line
- **Multi-thread support**: Tests stability with multiple event loops in multiple threads
- **Random parameters**: Prevents compiler optimization for realistic performance measurements