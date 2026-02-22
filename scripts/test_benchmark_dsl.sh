#!/bin/bash

# Test benchmark with DSL-generated API

echo "=== UVRPC Benchmark DSL Test ==="
echo ""

# Kill any existing processes
pkill -f benchmark 2>/dev/null || true
sleep 1

echo "[1/2] Starting publisher..."
./dist/bin/benchmark --publisher -a udp://127.0.0.1:6000 -p 1 -b 10 -d 3000 > /tmp/pub_dsl.log 2>&1 &
PUB_PID=$!
echo "Publisher PID: $PUB_PID"
sleep 1

echo "[2/2] Starting subscriber..."
./dist/bin/benchmark --subscriber -a udp://127.0.0.1:6000 -s 1 -d 3000 > /tmp/sub_dsl.log 2>&1 &
SUB_PID=$!
echo "Subscriber PID: $SUB_PID"

# Wait for subscriber to finish
wait $SUB_PID 2>/dev/null
sleep 1

# Kill publisher
kill $PUB_PID 2>/dev/null || true
sleep 1

echo ""
echo "=== Publisher Results ==="
grep -E "Messages|Throughput|Bandwidth|Publisher Results" /tmp/pub_dsl.log | tail -10
echo ""

echo "=== Subscriber Results ==="
grep -E "Messages|Throughput|Bandwidth|Subscriber Results" /tmp/sub_dsl.log | tail -10
echo ""

# Cleanup
pkill -f benchmark 2>/dev/null || true

echo "=== Test Complete ==="