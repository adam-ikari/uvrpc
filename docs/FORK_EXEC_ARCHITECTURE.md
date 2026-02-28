# Fork/Exec Architecture for Network Transports

## Overview

UVRPC benchmark supports fork/exec architecture for network transports (TCP, UDP, IPC) to ensure proper process isolation and libuv fork compatibility.

## Architecture Design

### Why Fork/Exec?

1. **Process Isolation**: Each test runs in an isolated server process
2. **Libuv Compatibility**: Static linking causes fork issues; dynamic linking with exec resolves this
3. **Clean State**: Each test starts with a fresh server process
4. **No State Contamination**: Tests don't interfere with each other

### Components

```
┌─────────────────────────────────────────────────────┐
│  Parent Process (Benchmark Runner)                  │
│  - Creates test configuration                       │
│  - Forks child process                             │
│  - Waits for server ready signal                   │
│  - Runs benchmark test                             │
│  - Kills server process on completion              │
└─────────────────────────────────────────────────────┘
                      │
                      │ fork()
                      ▼
┌─────────────────────────────────────────────────────┐
│  Child Process (Server)                             │
│  - exec() benchmark binary with --server flag      │
│  - Runs as independent server                      │
│  - Sends "ready" signal via pipe                   │
│  - Processes RPC requests                          │
│  - Killed by parent on completion                  │
└─────────────────────────────────────────────────────┘
```

## Implementation Details

### Server Mode

The benchmark binary supports a `--server` mode to run as a standalone server:

```bash
./dist/bin/uvrpc_benchmark --server --transport tcp --address tcp://127.0.0.1:5555
```

### Pipe Communication

Parent and child processes communicate via a Unix pipe:

1. **Child Process**:
   - Redirects pipe write end to stdout
   - Sends "ready\n" signal when server is initialized
   - Closes stderr to avoid interfering with pipe

2. **Parent Process**:
   - Reads from pipe until "ready" signal received
   - Continues with benchmark test
   - Kills server process on completion

### Event Loop Control

Uses `uv_async_t` for proper event loop stopping with `UV_RUN_DEFAULT`:

```c
void on_async_connection_complete(uv_async_t* handle) {
    benchmark_ctx_t* bench = (benchmark_ctx_t*)handle->data;
    __sync_bool_compare_and_swap(&bench->all_connected, 0, 1);
    uv_stop(bench->loop);
}

void on_connect(int status, void* ctx) {
    benchmark_ctx_t* bench = (benchmark_ctx_t*)ctx;
    if (bench->connected_count + bench->failed_connections >= bench->num_clients) {
        if (bench->async_handle) {
            uv_async_send(bench->async_handle);
        }
    }
}
```

## Key Fixes

### 1. Variable Scope Issue

**Problem**: `server_pid` was shadowed in fork branch:

```c
// Before (WRONG)
pid_t server_pid = fork();  // Local variable, shadows outer scope
```

**Fix**: Use outer scope variable:

```c
// After (CORRECT)
server_pid = fork();  // Uses outer scope variable
```

### 2. Double-Free Issue

**Problem**: `server_config` was freed twice:

```c
// Before (WRONG)
uvrpc_config_free(server_config);  // First free
// ... later in cleanup ...
uvrpc_config_free(server_config);  // Double-free!
```

**Fix**: Set to NULL after free:

```c
// After (CORRECT)
uvrpc_config_free(server_config);
server_config = NULL;  // Prevent double-free
```

### 3. Linking Issue

**Problem**: Static linking caused libuv fork compatibility issues

**Fix**: Changed to dynamic linking in `benchmark/CMakeLists.txt`:

```cmake
# Before
target_link_libraries(uvrpc_benchmark
    ${PROJECT_ROOT}/dist/lib/libuvrpc.a
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.a
    ...
)

# After
target_link_libraries(uvrpc_benchmark
    ${PROJECT_ROOT}/dist/lib/libuvrpc.so
    ${PROJECT_ROOT}/deps/libuv/build_shared/libuv.so
    ...
)
```

## Performance Results

Test results with fork+exec architecture (2026-02-28):

| Transport | Clients | Requests | Throughput | Avg Latency |
|-----------|---------|----------|------------|-------------|
| IPC | 10 | 10,000 | 83,467 ops/s | 0.012 ms |
| TCP | 10 | 10,000 | 52,103 ops/s | 0.019 ms |
| UDP | 10 | 10,000 | 72,244 ops/s | 0.014 ms |

High concurrency results:

| Transport | Clients | Requests | Throughput | Avg Latency |
|-----------|---------|----------|------------|-------------|
| IPC | 50 | 10,000 | 81,402 ops/s | 0.012 ms |
| TCP | 50 | 10,000 | 50,182 ops/s | 0.020 ms |
| UDP | 50 | 10,000 | 70,362 ops/s | 0.014 ms |

## Usage

### Running Benchmark

```bash
# Test IPC transport
./dist/bin/uvrpc_benchmark --transport ipc --requests 10000 --clients 10

# Test TCP transport
./dist/bin/uvrpc_benchmark --transport tcp --requests 10000 --clients 10

# Test UDP transport
./dist/bin/uvrpc_benchmark --transport udp --requests 10000 --clients 10
```

### Running Server Manually

```bash
# Start server manually for debugging
./dist/bin/uvrpc_benchmark --server --transport tcp --address tcp://127.0.0.1:5555
```

## Troubleshooting

### Server Process Not Terminating

If server processes remain after test completion:

```bash
# Kill all benchmark processes
pkill -9 uvrpc_benchmark
```

### Fork Fails

If fork() fails, check:
1. System resource limits (ulimit -u)
2. Available memory
3. libuv compatibility

### Timeout Issues

For high-concurrency tests, increase timeout:
- 10 clients: 60s timeout
- 50 clients: 180s timeout
- 100 clients: 300s timeout

## Future Improvements

1. **Process Pool**: Reuse server processes instead of creating new ones
2. **Graceful Shutdown**: Use SIGTERM instead of SIGKILL
3. **Health Checks**: Monitor server process health
4. **Resource Limits**: Set ulimit for server processes

## References

- [Libuv Fork Compatibility](https://docs.libuv.org/en/v1.x/guide/processes.html)
- [Benchmark Test Results](../../benchmark/PERFORMANCE_TEST_FINAL_REPORT.md)
- [Transport Layer Design](./UVBUS_UVRPC_ARCHITECTURE.md)

---

**Last Updated**: 2026-02-28