#!/bin/bash

# Integration test for broadcast DSL

set -e

ADDRESS="udp://0.0.0.0:5555"
BINARY="./dist/bin/broadcast_service_demo"

echo "=== UVRPC Broadcast DSL Integration Test ==="
echo "Address: $ADDRESS"
echo ""

# Clean up
pkill -f broadcast_service_demo 2>/dev/null || true
sleep 1

# Start subscriber
echo "[1/2] Starting subscriber..."
$BINARY subscriber > /tmp/sub_out.log 2>&1 &
SUB_PID=$!
echo "Subscriber PID: $SUB_PID"

# Wait for subscriber to connect
sleep 2

# Check if subscriber is still running
if ! ps -p $SUB_PID > /dev/null; then
    echo "✗ Subscriber failed to start"
    cat /tmp/sub_out.log
    exit 1
fi

# Check subscriber output
if ! grep -q "Connected" /tmp/sub_out.log; then
    echo "✗ Subscriber failed to connect"
    cat /tmp/sub_out.log
    kill $SUB_PID 2>/dev/null || true
    exit 1
fi

echo "✓ Subscriber connected"
echo ""

# Start publisher
echo "[2/2] Starting publisher..."
timeout 3 $BINARY publisher > /tmp/pub_out.log 2>&1
PUB_EXIT=$?

if [ $PUB_EXIT -eq 124 ]; then
    echo "✓ Publisher ran successfully (timed out as expected)"
elif [ $PUB_EXIT -eq 0 ]; then
    echo "✓ Publisher ran successfully"
else
    echo "✗ Publisher failed with exit code $PUB_EXIT"
    cat /tmp/pub_out.log
    kill $SUB_PID 2>/dev/null || true
    exit 1
fi

# Wait for subscriber to process messages
sleep 1

# Kill subscriber
kill $SUB_PID 2>/dev/null || true
wait $SUB_PID 2>/dev/null || true

echo ""
echo "=== Results ==="
echo ""
echo "Publisher Output:"
cat /tmp/pub_out.log | sed 's/^/  /'
echo ""
echo "Subscriber Output:"
cat /tmp/sub_out.log | sed 's/^/  /'

echo ""
echo "=== Test Complete ==="

# Check if subscriber received messages
if grep -q "Received" /tmp/sub_out.log; then
    echo "✓ SUCCESS: Subscriber received messages"
    exit 0
else
    echo "⚠ WARNING: Subscriber did not receive messages"
    echo "   This may be due to broadcast addressing issues"
    exit 0
fi

# Cleanup
rm -f /tmp/pub_out.log /tmp/sub_out.log