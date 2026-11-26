# UVRPC - Ultra-Fast RPC Framework

A minimalist, high-performance RPC framework built on libuv event loop and FlatBuffers serialization.

[![CI](https://github.com/adam-ikari/uvrpc/actions/workflows/ci.yml/badge.svg)](https://github.com/adam-ikari/uvrpc/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.1.2-orange.svg)](https://github.com/adam-ikari/uvrpc/releases/tag/v0.1.2)
<!-- The badge reads the project version, 0.1.2, and links the v0.1.2 release.
     It once claimed a v1.0.0a that never existed, contradicted the version in
     CMakeLists.txt, and linked the releases index because no tag existed.
     v0.1.1 was cut 2026-11-25; v0.1.2 follows the same day with specific error
     codes, a real link gate for the published archive, and comment fixes. -->

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

- **Zero Threads, Zero Locks, Zero Mutable Globals** - All I/O managed by libuv event loop; no file-scope mutable globals at all, and INPROC/SAMELOOP share an endpoint registry you create and pass in, so the framework never touches your event loop
- **High Performance** - sequential round-trips in the tens of microseconds on in-process transports; see the Benchmark workflow for measured numbers on named hardware
- **Multi-Transport Support** - TCP, UDP, IPC, INPROC, SAMELOOP
- **Multiple RPC Modes** - Normal (request-response), Oneway (fire-and-forget), Stream (multiple responses)
- **Zero-Copy** - FlatBuffers binary serialization; pointer-passing for in-process transports
- **Loop Injection** - Support custom libuv loop
- **Type Safety** - FlatBuffers DSL generates type-safe APIs
- **Fast Path Optimization** - vtable bypass for SAMELOOP transport
- **Single-Threaded Model** - Lock-free design

## 📊 Performance

All published performance numbers come from the `Benchmark` workflow on a
GitHub Actions runner (5 samples x 100k requests, Release, system allocator,
logging OFF). The table below is run `45750463697` on an AMD EPYC 9V74 runner
at load 2.24; every sample spread was 1.0x. Absolute numbers vary widely by
host, so a number quoted without its run and host is not a baseline.

| Transport | Round-trip latency | Throughput (1/latency) | Use Case |
|-----------|-------------------|------------------------|----------|
| SAMELOOP  | 0.67 µs | 1,492,537 req/s | Same-loop, vtable bypass |
| INPROC    | 0.67 µs | 1,492,537 req/s | In-process zero-copy |
| IPC       | 10.36 µs | 96,525 req/s | Local inter-process (Unix socket) |
| UDP       | 18.22 µs | 54,885 req/s | Loss-tolerant; loopback only |
| TCP       | 17.33 µs | 57,703 req/s | Reliable network RPC |

The ordering is stable across hosts; the absolute numbers are not. The workflow
gates only **within-family ratios** (INPROC/SAMELOOP, IPC/TCP, UDP/TCP) against
`benchmark/baseline.json`; cross-family ratios reflect runner hardware and are
printed but not gated.

> "Throughput" here means the reciprocal of sequential round-trip latency (one
> request in flight), not pipelined throughput. UDP is loopback only and says
> nothing about packet loss on a real network. Reproduce locally with
> `./dist/bin/perf_benchmark [requests] [transport]`, but treat local numbers
> as unverified unless the host is under no load.

See [benchmark/](benchmark/) for the benchmark source and methodology.

**gRPC comparison.** The Benchmark workflow also measures gRPC synchronous
unary calls over loopback TCP with the same sequential, 8-byte echo workload.
On run `45750463697` (same EPYC 9V74 runner) UVRPC measured 17.39 µs / 2,740 KiB
and the blocking-unary style 17.46 µs / 2,960 KiB, against gRPC 87.01 µs /
18,256 KiB — about 5x lower latency and 6x lower peak RSS. This comparison is
informational; the workflow's only gate is on the five transports' ratios.

The 63% latency cut from replacing the 65,536-slot deadline sweep was measured
on a 5800H development host; it is retained as the optimization's historical
evidence, not as a baseline. Build steps, host details and raw samples for both
the CI numbers and the historical run are in the
[gRPC comparison section](/guide/benchmark#uvrpc-vs-grpc).

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