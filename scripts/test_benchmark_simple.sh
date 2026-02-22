#!/bin/bash

echo "=== UVRPC Benchmark DSL Test (Simple) ==="
echo ""

# Kill any existing processes
pkill -f benchmark 2>/dev/null || true
sleep 1

echo "[1/2] Starting publisher (500ms)..."
timeout 2 ./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 1 -b 10 -d 500 > /tmp/pub_simple.log 2>&1 &
PUB_PID=$!
wait $PUB_PID 2>/dev/null
EXIT_CODE=$?

echo ""
echo "=== Publisher Results ==="
cat /tmp/pub_simple.log | tail -20

echo ""
echo "=== Test Complete ==="