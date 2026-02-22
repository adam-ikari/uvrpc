#!/bin/bash

set -e

echo "=========================================="
echo "UVRPC Oneway vs Regular RPC Comparison"
echo "=========================================="

cd "$(dirname "$0")/.."

# Generate code
echo "Generating RPC code..."
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc schema/benchmark_server.fbs -o generated

# Compile
echo "Compiling..."
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    benchmark/test_oneway_performance.c \
    generated/benchmark_benchmarkservice_client.c \
    -Lbuild -Ldeps/mimalloc/out/release \
    -luvrpc -luv -lmimalloc -lpthread \
    -o test_oneway_perf -O2

# Start server
echo ""
echo "Starting benchmark server..."
./dist/bin/benchmark_service &
SERVER_PID=$!
sleep 2

echo ""
echo "=========================================="
echo "Running Oneway Performance Test"
echo "=========================================="
echo "Oneway methods don't wait for response"
echo "=========================================="

# Run test
./test_oneway_perf

# Cleanup
echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo "Oneway methods provide:"
echo "  - Higher throughput (no response wait)"
echo "  - Lower latency (no round-trip)"
echo "  - Better scalability for fire-and-forget operations"
echo ""
echo "Use oneway methods for:"
echo "  - Logging"
echo "  - Notifications"
echo "  - Events"
echo "  - Metrics"
echo "=========================================="
