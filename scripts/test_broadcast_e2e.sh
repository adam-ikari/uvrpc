#!/bin/bash

set -e

cd "$(dirname "$0")/.."

echo "=== UVRPC Broadcast End-to-End Test ==="
echo ""

# Kill any existing processes
pkill -f broadcast_service_demo 2>/dev/null || true
sleep 1

# Start publisher in background
echo "[1/2] Starting publisher..."
./dist/bin/broadcast_service_demo publisher > /tmp/pub_e2e.log 2>&1 &
PUB_PID=$!
echo "Publisher PID: $PUB_PID"
sleep 2

# Check if publisher started
if ! kill -0 $PUB_PID 2>/dev/null; then
    echo "✗ Publisher failed to start"
    cat /tmp/pub_e2e.log
    exit 1
fi
echo "✓ Publisher is running"
echo ""

# Start subscriber
echo "[2/2] Starting subscriber..."
timeout 15 ./dist/bin/broadcast_service_demo subscriber > /tmp/sub_e2e.log 2>&1
SUB_EXIT=$?

# Check subscriber output
echo ""
echo "=== Subscriber Output ==="
tail -50 /tmp/sub_e2e.log
echo ""

echo "=== Publisher Output ==="
cat /tmp/pub_e2e.log
echo ""

# Cleanup
pkill -f broadcast_service_demo 2>/dev/null || true

if [ $SUB_EXIT -eq 0 ]; then
    echo "✓ Test completed successfully"
else
    echo "✗ Test failed with exit code: $SUB_EXIT"
    exit 1
fi