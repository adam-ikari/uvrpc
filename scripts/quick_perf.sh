#!/bin/bash

# Quick performance test using simple_client
ADDRESS=${1:-"tcp://127.0.0.1:54321"}
ITERATIONS=${2:-10}

# Kill any existing processes
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null
sleep 1

# Start server
./dist/bin/simple_server $ADDRESS > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server is listening
if ! lsof -i :54321 2>/dev/null | grep -q LISTEN; then
    echo "ERROR: Server not listening"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo "Running $ITERATIONS iterations..."

# Run test
START=$(date +%s%N)
for i in $(seq 1 $ITERATIONS); do
    ./dist/bin/simple_client $ADDRESS > /tmp/client_$i.log 2>&1
    if ! grep -q "Result: 30" /tmp/client_$i.log; then
        echo "ERROR: Request $i failed"
    fi
done
END=$(date +%s%N)

ELAPSED=$(( (END - START) / 1000000 ))
OPS=$(( ITERATIONS * 1000000 / ELAPSED ))

echo ""
echo "=== Results ==="
echo "Iterations: $ITERATIONS"
echo "Time: ${ELAPSED}ms"
echo "Throughput: ~${OPS} ops/s"

# Cleanup
kill $SERVER_PID 2>/dev/null
rm -f /tmp/client_*.log /tmp/server.log