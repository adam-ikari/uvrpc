# Log Service Example

This example demonstrates how to use oneway RPC methods in UVRPC by implementing a distributed logging service.

## Overview

The Log Service shows:
- **Oneway RPC methods** - Fire-and-forget logging without waiting for responses
- **Structured logging** - Log levels, timestamps, source tracking
- **Batch logging** - Send multiple log entries in a single request
- **High-performance logging** - Non-blocking, asynchronous log delivery

## Schema Definition

Located at `schema/log_service.fbs`:

```flatbuffers
namespace log;

enum LogLevel:byte {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4
}

table LogEntry {
    level: LogLevel;
    message: string;
    timestamp: int64;
    source: string;
    thread_id: uint64;
    component: string;
}

table EmptyResponse {}

rpc_service LogService {
    Log(LogEntry):EmptyResponse;          /* Single log - oneway */
    LogBatch(LogBatchRequest):EmptyResponse; /* Batch logs - oneway */
    QuickLog(QuickLogRequest):EmptyResponse;  /* Quick log - oneway */
}
```

## Key Points

### Why Oneway for Logging?

1. **Performance**: Clients don't wait for acknowledgment, improving throughput
2. **Asynchronous**: Logging doesn't block the calling thread
3. **Scalability**: High-volume logging doesn't slow down the application
4. **Simplicity**: No need to handle responses or callbacks

### Generated API

```c
/* All methods are oneway - use struct parameters */
uvrpc_error_t uvrpc_logservice_Log(uvrpc_client_t* client,
                                    const log_LogEntry_t* request);

uvrpc_error_t uvrpc_logservice_LogBatch(uvrpc_client_t* client,
                                         const log_LogBatchRequest_t* request);

uvrpc_error_t uvrpc_logservice_QuickLog(uvrpc_client_t* client,
                                         const log_QuickLogRequest_t* request);
```

Note: No `*_sync()` variants are generated for oneway methods.

## Building

1. Generate code:
```bash
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc \
    schema/log_service.fbs -o generated/log_service
```

2. Compile:
```bash
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include \
    -Ideps/mimalloc/include -Lbuild -Ldeps/mimalloc/out/release \
    examples/log_service_demo.c \
    generated/log_service/log_logservice_client.c \
    generated/log_service/log_logservice_server_stub.c \
    generated/log_service/log_logservice_rpc_common.h \
    -o log_service_demo -luvrpc -luv -lmimalloc -lpthread
```

3. Run:
```bash
./log_service_demo
```

## Examples

### Simple Demo (`log_simple_demo.c`)

Minimal example showing basic oneway logging:
- Single log entry
- Different log levels
- Server displays formatted logs

### Full Demo (`log_service_demo.c`)

Comprehensive example with:
- Single log entries with full metadata
- Quick log messages (simplified API)
- Batch logging (multiple entries in one call)
- High-volume logging (10+ messages)
- Thread ID tracking
- Component/source identification

## Usage Example

```c
/* Single log entry */
log_LogEntry_t entry = {
    .level = 1,  /* INFO */
    .message = "Application started",
    .timestamp = time(NULL),
    .source = "main",
    .thread_id = 0,
    .component = "app"
};
uvrpc_logservice_Log(client, &entry);

/* Quick log */
log_QuickLogRequest_t quick = {
    .level = 1,
    .message = "Quick info message"
};
uvrpc_logservice_QuickLog(client, &quick);

/* Batch log */
log_LogEntry_t entries[2];
log_LogEntry_t entry1 = { .level = 1, .message = "Log 1", ... };
log_LogEntry_t entry2 = { .level = 2, .message = "Log 2", ... };
entries[0] = entry1;
entries[1] = entry2;

log_LogBatchRequest_t batch = {
    .entries = entries,
    .entries_size = 2
};
uvrpc_logservice_LogBatch(client, &batch);
```

## Output Example

```
Starting Log Service Server...
Log Service started on tcp://127.0.0.1:6666

=== Demo 1: Single Log Messages ===
[2026-02-21 10:30:45] [INFO] [main] [Thread:12345] app: Application started successfully
[2026-02-21 10:30:45] [WARNING] [config] [Thread:12345] config-loader: Configuration file not found
[2026-02-21 10:30:45] [ERROR] [db] [Thread:12345] db-connector: Failed to connect to database

=== Demo 2: Quick Log Messages ===
[QUICK] [INFO] Quick info message
[QUICK] [WARNING] Quick warning message
[QUICK] [ERROR] Quick error message

=== Demo 3: Batch Log Messages ===
=== Batch Log: 3 entries ===
  [INFO] [auth] Batch entry 1: User logged in
  [INFO] [payment] Batch entry 2: Transaction processed
  [WARNING] [system] Batch entry 3: Low memory warning
```

## Use Cases

1. **Application Logging**: Centralized logging service for microservices
2. **Audit Trails**: Record events without affecting transaction latency
3. **Metrics Collection**: Send performance metrics asynchronously
4. **Event Streaming**: Fire events to a logging service
5. **Debug Tracing**: Debug logs that don't impact production performance

## Comparison: Oneway vs Regular RPC

| Aspect | Oneway (Log) | Regular RPC |
|--------|--------------|-------------|
| Response Required | No | Yes |
| Callback Needed | No | Yes |
| Blocking | Non-blocking | May block |
| Use Case | Logging, Events | Data Retrieval |
| Generated API | Single function | + *_sync() variant |

## Best Practices

1. **Error Handling**: Oneway calls return immediately, handle errors at call site
2. **Batching**: Use batch methods for high-volume logging
3. **Levels**: Use appropriate log levels (DEBUG, INFO, WARNING, ERROR, FATAL)
4. **Metadata**: Include source, component, and thread ID for tracing
5. **Timestamps**: Always include timestamps for log ordering

## Related Examples

- `test_oneway.c` - Basic oneway testing with BenchmarkService
- `rpc_dsl_demo.c` - Regular RPC methods with request/response pattern