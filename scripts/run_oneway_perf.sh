#!/bin/bash

set -e

echo "=========================================="
echo "UVRPC Oneway Performance Test"
echo "=========================================="

cd "$(dirname "$0")/.."

# Start server in background
echo "Starting benchmark server..."
./dist/bin/simple_server tcp://127.0.0.1:5555 > /tmp/oneway_server.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Failed to start server"
    cat /tmp/oneway_server.log
    exit 1
fi

echo ""
echo "=========================================="
echo "Running Oneway Performance Test"
echo "=========================================="

# Run the test (mode 1 = oneway only)
./test_oneway_perf tcp://127.0.0.1:5555 1

echo ""
echo "=========================================="
echo "Cleaning Up"
echo "=========================================="

# Stop server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "Test completed!"