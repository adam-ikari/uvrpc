#!/bin/bash
# Simple performance test

echo "=== UVRPC Performance Test ==="

# Start server
./dist/bin/server tcp://127.0.0.1:5555 > /tmp/server.log 2>&1 &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"
sleep 2

# Run client test
echo "Running client test..."
timeout 5 ./dist/bin/client tcp://127.0.0.1:5555 -d 1000 2>&1 | grep -E "(Throughput|Success|Failed)"

# Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo "Test completed"