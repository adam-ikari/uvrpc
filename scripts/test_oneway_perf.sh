#!/bin/bash

set -e

echo "=========================================="
echo "UVRPC Oneway Performance Test"
echo "=========================================="

# Generate code first
echo "Generating RPC code..."
cd "$(dirname "$0")/.."
python3 tools/uvrpcc.py --flatcc deps/flatcc/bin/flatcc schema/benchmark_server.fbs -o generated

# Compile the test
echo "Compiling performance test..."
gcc -I. -Igenerated -Iinclude -Ideps/libuv/include -Ideps/mimalloc/include \
    benchmark/test_oneway_performance.c \
    generated/benchmark_benchmarkservice_client.c \
    -Lbuild -Ldeps/mimalloc/out/release \
    -luvrpc -luv -lmimalloc -lpthread \
    -o test_oneway_perf -O2

echo ""
echo "=========================================="
echo "Starting Benchmark Server"
echo "=========================================="
# Start benchmark server in background
./dist/bin/benchmark_service &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to start
sleep 2

echo ""
echo "=========================================="
echo "Running Performance Test"
echo "=========================================="
# Run the performance test
./test_oneway_perf

echo ""
echo "=========================================="
echo "Cleaning Up"
echo "=========================================="
# Kill the server
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "Test completed!"
