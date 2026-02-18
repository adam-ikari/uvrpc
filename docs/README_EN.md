# UVRPC - Ultra-Fast RPC Framework

A minimalist, high-performance RPC framework built on libuv event loop and FlatBuffers serialization.

## Design Philosophy

### Core Principles

1. **Minimal Design** - Minimized API, dependencies, and configuration
2. **Zero Threads, Zero Locks, Zero Global Variables** - All I/O managed by libuv event loop
3. **Performance-Driven** - Zero-copy, event-driven, high-performance memory allocation
4. **Transparency First** - Public structures, user has full control
5. **Loop Injection** - Support custom libuv loop, multiple instances can run independently or share loop
6. **Unified Async** - All calls are asynchronous, no synchronous blocking mode
7. **Unified Multi-Protocol Abstraction** - Support TCP, UDP, IPC, INPROC, identical usage

### Architecture Layers

```
┌─────────────────────────────────────────────────────┐
│  Layer 3: Application Layer (User Created)           │
│  - Service handlers (uvrpc_handler_t)               │
│  - Client callbacks (uvrpc_callback_t)              │
│  - Completely independent, not affected by generation │
├─────────────────────────────────────────────────────┤
│  Layer 2: RPC API Layer                             │
│  - Server API (uvrpc_server_t)                      │
│  - Client API (uvrpc_client_t)                      │
│  - Publish/Subscribe API (uvrpc_publisher/subscriber_t) │
│  - Unified configuration (uvrpc_config_t)            │
├─────────────────────────────────────────────────────┤
│  Layer 1: Transport Layer                           │
│  - TCP (uv_tcp_t)                                   │
│  - UDP (uv_udp_t)                                   │
│  - IPC (uv_pipe_t)                                  │
│  - INPROC (In-process communication)                 │
├─────────────────────────────────────────────────────┤
│  Layer 0: Core Library Layer                         │
│  - Event loop (libuv)                               │
│  - Serialization (FlatCC/FlatBuffers)                │
│  - Memory allocation (mimalloc/system/custom)       │
│  - Performance optimization (zero-copy, batch)       │
└─────────────────────────────────────────────────────┘
```

## Features

- **Minimal Design**: Clear API, builder pattern configuration
- **High Performance**: Efficient serialization based on libuv and FlatBuffers
- **Event-Driven**: Based on libuv, fully asynchronous and non-blocking
- **Multi-Transport Support**: TCP, UDP, IPC, INPROC
- **Zero-Copy**: FlatBuffers binary serialization, minimize memory copies
- **Loop Injection**: Support custom libuv loop, multiple instances can run independently or share loop
- **Memory Allocator**: Support mimalloc, system allocator, custom allocator
- **Type Safety**: FlatBuffers DSL generates type-safe APIs with compile-time checking
- **Code Generation**: Declare services using FlatBuffers DSL, auto-generate client/server code
- **Multi-Instance Support**: Create multiple independent instances in same process, support independent or shared event loops
- **Single-Threaded Model**: Lock-free design, avoid critical section communication

## Performance Metrics

### CS Mode (Client-Server)

| Transport | Throughput (ops/s) | Avg Latency | Memory | Success Rate | Use Case |
|-----------|-------------------|-------------|--------|--------------|----------|
| **INPROC** | 125,000+ | 0.03 ms | 1 MB | 100% | High-performance in-process communication |
| **IPC** | 91,895 | 0.10 ms | 2 MB | 100% | Local inter-process communication |
| **UDP** | 91,685 | 0.15 ms | 2 MB | 100% | High-throughput network (acceptable packet loss) |
| **TCP** | 86,930 | 0.18 ms | 2 MB | 100% | Reliable network transmission |

### Broadcast Mode (Publish-Subscribe)

| Transport | Throughput (msgs/s) | Bandwidth (MB/s) | Use Case |
|-----------|---------------------|------------------|----------|
| **IPC** | 42,500 | 4.25 | Local broadcast |
| **UDP** | 40,000 | 4.00 | Network broadcast |
| **TCP** | Supported | Supported | Reliable broadcast |

### Performance Characteristics
- **INPROC**: Zero-copy synchronous execution, suitable for high-performance in-process communication, extremely low latency
- **IPC**: No network overhead, suitable for local inter-process communication, better performance than network transmission
- **UDP**: Connectionless protocol, suitable for high-throughput, loss-tolerant scenarios, low latency
- **TCP**: Reliable transmission, suitable for scenarios requiring data integrity

### Test Configuration
- Build Mode: Release (-O2 optimization, no debug symbols)
- Test Duration: 2 seconds
- Batch Size: 50-100 requests
- Client Count: 1-10
- Success Rate: 100% for all transports

**Note**: Actual performance depends on hardware configuration, network conditions, and load scenarios. For detailed performance analysis, see [benchmark/results/PERFORMANCE_ANALYSIS.md](../../benchmark/results/PERFORMANCE_ANALYSIS.md).

## Dependencies

- libuv (>= 1.0) - Event loop
- FlatCC - FlatBuffers compiler
- uthash - Hash table
- mimalloc (optional) - High-performance memory allocator
- gtest (testing) - Unit testing framework

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install (optional)
sudo make install
```

### Basic Usage

**Server:**

```c
#include "uvrpc.h"

void echo_handler(uvrpc_request_t* req, void* ctx) {
    // Echo back the request
    uvrpc_response_send(req, req->params, req->params_size);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    // Create server
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

    uvrpc_server_t* server = uvrpc_server_create(config);
    uvrpc_server_register(server, "echo", echo_handler, NULL);
    uvrpc_server_start(server);

    // Run event loop
    uv_run(&loop, UV_RUN_DEFAULT);

    // Cleanup
    uvrpc_server_free(server);
    uvrpc_config_free(config);
    uv_loop_close(&loop);

    return 0;
}
```

**Client:**

```c
#include "uvrpc.h"

void response_callback(uvrpc_response_t* resp, void* ctx) {
    printf("Received response: %.*s\n", (int)resp->result_size, resp->result);
}

int main() {
    uv_loop_t loop;
    uv_loop_init(&loop);

    // Create client
    uvrpc_config_t* config = uvrpc_config_new();
    uvrpc_config_set_loop(config, &loop);
    uvrpc_config_set_address(config, "tcp://127.0.0.1:5555");

    uvrpc_client_t* client = uvrpc_client_create(config);
    uvrpc_client_connect(client);

    // Call RPC method
    const char* message = "Hello, UVRPC!";
    uvrpc_client_call(client, "echo", 
                      (const uint8_t*)message, strlen(message),
                      response_callback, NULL);

    // Run event loop
    uv_run(&loop, UV_RUN_DEFAULT);

    // Cleanup
    uvrpc_client_free(client);
    uvrpc_config_free(config);
    uv_loop_close(&loop);

    return 0;
}
```

## Documentation

- [Quick Start Guide](QUICK_START.md) - 5-minute tutorial
- [API Guide](API_GUIDE.md) - Complete API documentation
- [API Reference](API_REFERENCE.md) - Function reference
- [Build and Install](BUILD_AND_INSTALL.md) - Build instructions
- [Design Philosophy](DESIGN_PHILOSOPHY.md) - Architecture and design
- [Migration Guide](MIGRATION_GUIDE.md) - Version migration guide
- [Doxygen Documentation](../doxygen/html/index.html) - Generated API docs

## Examples

See [examples/](../../examples/) directory for complete examples:

- `simple_server.c` - Simple echo server
- `simple_client.c` - Simple echo client
- `async_await_demo.c` - Async/await pattern
- `broadcast_publisher.c` - Broadcast publisher
- `broadcast_subscriber.c` - Broadcast subscriber
- `complete_example.c` - Full-featured example

## Performance Testing

Run performance benchmarks:

```bash
cd benchmark
./run_benchmark.sh
```

See [benchmark/README.md](../../benchmark/BENCHMARK_README.md) for details.

## Contributing

Contributions are welcome! Please read [CODING_STANDARDS.md](CODING_STANDARDS.md) before submitting.

## License

MIT License - see [LICENSE](../../LICENSE) for details

## Author

UVRPC Team

## Acknowledgments

- [libuv](https://libuv.org/) - Cross-platform asynchronous I/O
- [FlatBuffers](https://google.github.io/flatbuffers/) - Efficient serialization
- [mimalloc](https://github.com/microsoft/mimalloc) - High-performance allocator
- [uthash](https://troydhanson.github.io/uthash/) - Hash table implementation