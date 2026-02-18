# UVRPC - Ultra-Fast RPC Framework

A minimalist, high-performance RPC framework built on libuv event loop and FlatBuffers serialization.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/adam-ikari/uvrpc)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-orange.svg)](https://github.com/adam-ikari/uvrpc)

## 🚀 Quick Start

```bash
# Clone and build
git clone https://github.com/adam-ikari/uvrpc.git
cd uvrpc
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run examples
./simple_server &
./simple_client
```

## 📚 Documentation

### English Documentation
Complete documentation is available in [docs/en/](docs/en/):

- [README.md](docs/en/README.md) - Project Overview
- [Quick Start Guide](docs/en/QUICK_START.md) - 5-minute tutorial
- [API Guide](docs/en/API_GUIDE.md) - Complete API documentation
- [Build & Install](docs/en/BUILD_AND_INSTALL.md) - Build instructions
- [Doxygen Docs](docs/doxygen/html/index.html) - Generated API docs

### Chinese Documentation (中文文档)
中文文档正在开发中，详见 [docs/zh/](docs/zh/README.md)。

## ✨ Features

- **Zero Threads, Zero Locks, Zero Global Variables** - All I/O managed by libuv event loop
- **High Performance** - 125,000+ ops/s (INPROC), 86,930 ops/s (TCP)
- **Multi-Transport Support** - TCP, UDP, IPC, INPROC
- **Zero-Copy** - FlatBuffers binary serialization
- **Loop Injection** - Support custom libuv loop
- **Type Safety** - FlatBuffers DSL generates type-safe APIs
- **Code Generation** - Auto-generate client/server code
- **Single-Threaded Model** - Lock-free design

## 📊 Performance

| Transport | Throughput | Latency | Use Case |
|-----------|-----------|---------|----------|
| INPROC | 125,000+ ops/s | 0.03 ms | In-process |
| IPC | 91,895 ops/s | 0.10 ms | Local IPC |
| UDP | 91,685 ops/s | 0.15 ms | High-throughput |
| TCP | 86,930 ops/s | 0.18 ms | Reliable network |

See [benchmark/](benchmark/) for detailed performance analysis.

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

- [Architecture](docs/en/DESIGN_PHILOSOPHY.md)
- [API Reference](docs/en/API_REFERENCE.md)
- [Coding Standards](docs/en/CODING_STANDARDS.md)
- [Migration Guide](docs/en/MIGRATION_GUIDE.md)

## 🤝 Contributing

Contributions are welcome! Please read [CODING_STANDARDS.md](docs/en/CODING_STANDARDS.md).

## 📄 License

MIT License - see [LICENSE](LICENSE) for details

## 👥 Acknowledgments

- [libuv](https://libuv.org/)
- [FlatBuffers](https://google.github.io/flatbuffers/)
- [mimalloc](https://github.com/microsoft/mimalloc)

---

**UVRPC Team** - 2026