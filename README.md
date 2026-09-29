# UVRPC - Ultra-Fast RPC Framework

A minimalist, high-performance RPC framework built on libuv event loop and FlatBuffers serialization.

[![CI](https://github.com/adam-ikari/uvrpc/actions/workflows/ci.yml/badge.svg)](https://github.com/adam-ikari/uvrpc/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0a-orange.svg)](https://github.com/adam-ikari/uvrpc/releases/tag/v1.0.0a)

## 🚀 Quick Start

```bash
# Clone and build
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc

# Build vendored deps (libuv, flatcc, mimalloc, gtest) from submodules
./scripts/setup_deps.sh

# Configure + build (default: Release, mimalloc allocator)
./build.sh

# Run examples
./dist/bin/simple_server &
./dist/bin/simple_client
```

`setup_deps.sh` clones and builds the submodules; `build.sh` runs `cmake -S . -B build`
with the default allocator. For the system allocator instead of mimalloc, use
`./build.sh release system`.

## 📚 Documentation

### 🌐 Online Documentation
- **Official Website**: [https://adam-ikari.github.io/uvrpc/](https://adam-ikari.github.io/uvrpc/)
- **中文文档**: [https://adam-ikari.github.io/uvrpc/zh/](https://adam-ikari.github.io/uvrpc/zh/)

### Documentation
- [Quick Start Guide](https://adam-ikari.github.io/uvrpc/quick-start) - 5-minute tutorial
- [API Guide](https://adam-ikari.github.io/uvrpc/guide/api-guide) - Complete API documentation
- [Build & Install](https://adam-ikari.github.io/uvrpc/build-install) - Build instructions
- [Design Philosophy](https://adam-ikari.github.io/uvrpc/guide/design-philosophy) - Architecture & design principles
- [Benchmark](https://adam-ikari.github.io/uvrpc/guide/benchmark) - Performance methodology & numbers
- [Coding Standards](https://adam-ikari.github.io/uvrpc/development/coding-standards) - Contribution guidelines

## ✨ Features

- **Zero Threads, Zero Locks, Zero Mutable Globals** - All I/O managed by libuv event loop; no file-scope mutable globals in system/mimalloc builds (custom allocator builds have exactly one, and INPROC/SAMELOOP keep their endpoint registry on `loop->data`)
- **High Performance** - ~1,000,000 req/s sequential round-trip on SAMELOOP/INPROC (~1.0 µs latency, measured on a CI runner)
- **Multi-Transport Support** - TCP, UDP, IPC, INPROC, SAMELOOP
- **Multiple RPC Modes** - Normal (request-response), Oneway (fire-and-forget), Stream (multiple responses)
- **Zero-Copy** - FlatBuffers binary serialization; pointer-passing for in-process transports
- **Loop Injection** - Support custom libuv loop
- **Type Safety** - FlatBuffers DSL generates type-safe APIs
- **Fast Path Optimization** - vtable bypass for SAMELOOP transport
- **Single-Threaded Model** - Lock-free design

## 📊 Performance

Sequential ping-pong (one request in flight), 8-byte payload, Release build, single thread, measured by the `Benchmark` workflow on a GitHub Actions runner. Absolute numbers vary widely by host.

| Transport | Round-trip latency | Throughput (1/latency) | Use Case |
|-----------|-------------------|------------------------|----------|
| SAMELOOP  | 1.00 µs | ~1,000,000 req/s | Same-loop, vtable bypass (fastest) |
| INPROC    | 1.02 µs | ~980,000 req/s | In-process zero-copy |
| IPC       | 10.76 µs | ~93,000 req/s | Local inter-process (Unix socket) |
| UDP       | 20.10 µs | ~50,000 req/s | Loss-tolerant, high-throughput |
| TCP       | 20.08 µs | ~50,000 req/s | Reliable network RPC |

> Note: "throughput" above is the reciprocal of sequential round-trip latency (one request in flight), not pipelined throughput. Run `./dist/bin/perf_benchmark [requests] [transport]` to reproduce.

See [benchmark/](benchmark/) for the benchmark source and methodology.

## 🎯 Examples

```c
// Server
uvrpc_server_t* server = uvrpc_server_create(config);
uvrpc_server_register(server, "echo", echo_handler, NULL);
uvrpc_server_start(server);

// Client
uvrpc_client_t* client = uvrpc_client_create(config);
uvrpc_client_connect(client);
uvrpc_client_call(client, "echo", data, size, callback, NULL);
```

See [examples/](examples/) for complete examples.

## 🛠️ Dependencies

- libuv (>= 1.0)
- FlatCC
- uthash
- mimalloc (optional)

## 📖 Learn More

- [Architecture](https://adam-ikari.github.io/uvrpc/architecture/)
- [API Reference](https://adam-ikari.github.io/uvrpc/api/)
- [Single Thread Model](https://adam-ikari.github.io/uvrpc/guide/single-thread-model)
- [Coding Standards](https://adam-ikari.github.io/uvrpc/development/coding-standards)
- [Primitives Guide](docs/PRIMITIVES_GUIDE.md)

## 🤝 Contributing

Contributions are welcome! Please read [coding standards](docs/development/coding-standards.md).

## 📄 License

MIT License - see [LICENSE](LICENSE) for details

## 👥 Acknowledgments

- [libuv](https://libuv.org/)
- [FlatBuffers](https://google.github.io/flatbuffers/)
- [mimalloc](https://github.com/microsoft/mimalloc)

---

**UVRPC Team** - 2026