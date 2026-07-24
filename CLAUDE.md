# UVRPC Project

High-performance RPC framework built on libuv + FlatBuffers.
Core philosophy: zero threads, zero locks, zero global variables.

## Build Commands

```bash
# Standard build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Debug build with logging
cmake .. -DCMAKE_BUILD_TYPE=Debug -DUVRPC_DEBUG_LOGGING=ON

# Build with tests
cmake .. -DUVRPC_BUILD_TESTS=ON
make -j$(nproc)

# Build with examples (on by default)
cmake .. -DUVRPC_BUILD_EXAMPLES=ON
```

## Test Commands

```bash
# Run all tests (requires -DUVRPC_BUILD_TESTS=ON)
cd build && ctest --output-on-failure

# Run specific test binary
./build/dist/bin/uvrpc_tests

# Run integration tests
./tests/run_all_tests.sh
```

## Key Design Principles

- **Zero threads**: All I/O managed by libuv event loop. Never add threading, thread pools, or background workers.
- **Zero locks**: Single-threaded model makes locks unnecessary. Never add mutexes, spinlocks, or atomics.
- **Zero global variables**: All state lives in context objects (server, client, config structs). No file-scope mutable globals.

## Directory Structure

```
include/          Public API headers (uvrpc.h, uvbus.h, etc.)
src/              Implementation files and private headers
generated/        FlatCC-generated code (created at build time, NEVER edit manually)
schema/           FlatBuffers schema definitions (.fbs)
examples/         Standalone example programs (scenario_*.c are runnable demos)
tests/            Unit tests (GTest), integration tests, e2e tests
benchmark/        Performance benchmarks
tools/            Code generator and DSL tools (Python)
deps/             Vendored dependencies (libuv, flatcc, uthash, mimalloc, gtest)
docs/             Project documentation
```

## Coding Standards

- All comments, docs, and commits in English
- Doxygen for all public API functions
- `uvrpc_` prefix for public functions/types, `UVRPC_` for macros/enums
- snake_case for functions/types, UPPER_SNAKE for macros
- Conventional Commits style: `feat(scope):`, `fix(scope):`, `refactor(scope):`
- Ring buffer sizes must be powers of 2
- Error codes returned via `uvrpc_error_t`, never silently swallowed

## Key Files

- `include/uvrpc.h` - Main public API header
- `include/uvbus.h` - Transport/bus layer API
- `CMakeLists.txt` - Build configuration
- `docs/DESIGN_PHILOSOPHY.md` - Architecture details
- `docs/CODING_STANDARDS.md` - Full coding standards

## Transports

| Transport | Address Format | Use Case |
|-----------|---------------|----------|
| TCP       | `tcp://host:port` | Reliable network RPC |
| UDP       | `udp://host:port` | High-throughput, loss-tolerant |
| IPC       | `ipc://path` | Local inter-process |
| INPROC    | `inproc://name` | In-process zero-copy |
| SAMELOOP  | `sameloop://name` | Same loop, vtable bypass |

INPROC and SAMELOOP are same-loop, single-threaded transports. Server and client
discover each other by name through a per-loop registry stored on `loop->data`.
This means: (1) the server and client MUST share the same `uv_loop_t*`; (2) the
loop's `data` slot is reserved for the framework when using these transports —
zero-initialize stack-allocated loops (`uv_loop_t loop = {0};`) before
`uv_loop_init`, since libuv preserves `loop->data` across `uv_loop_init`. There
are no locks (single-threaded) and no file-scope globals — the registry is
per-loop and freed when the last transport on the loop is destroyed.

## Important Notes

- C99 standard (not C++)
- Tests use GTest (C++) for unit tests, custom assert for C tests
- Default allocator is mimalloc (`-DUVRPC_ALLOCATOR_DEFAULT=system` for system malloc)
- Output goes to `dist/bin/` and `dist/lib/`
- Static linking by default
