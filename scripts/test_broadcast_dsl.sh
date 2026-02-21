#!/bin/bash

# UVRPC Broadcast DSL Test Script

set -e

ADDRESS="udp://0.0.0.0:5555"
BINARY="./dist/bin/broadcast_service_demo"

echo "=== UVRPC Broadcast DSL Test ==="
echo "Address: $ADDRESS"
echo ""

# Clean up any existing processes
pkill -f broadcast_service_demo 2>/dev/null || true
sleep 1

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found: $BINARY"
    echo "Please build the project first: make broadcast_service_demo"
    exit 1
fi

# Start subscriber in background
echo "[1/2] Starting subscriber..."
$BINARY subscriber > /tmp/subscriber.log 2>&1 &
SUB_PID=$!
echo "Subscriber PID: $SUB_PID"
sleep 2

# Check if subscriber is running
if ! ps -p $SUB_PID > /dev/null; then
    echo "Error: Subscriber failed to start"
    cat /tmp/subscriber.log
    exit 1
fi

echo "Subscriber started successfully"
echo ""

# Start publisher
echo "[2/2] Starting publisher..."
$BINARY publisher > /tmp/publisher.log 2>&1 &
PUB_PID=$!
echo "Publisher PID: $PUB_PID"
sleep 3

# Check if publisher is running
if ! ps -p $PUB_PID > /dev/null; then
    echo "Error: Publisher failed to start"
    cat /tmp/publisher.log
    kill $SUB_PID 2>/dev/null || true
    exit 1
fi

echo "Publisher started successfully"
echo ""

# Wait for processes to complete
wait $PUB_PID 2>/dev/null || true
wait $SUB_PID 2>/dev/null || true

# Display results
echo ""
echo "=== Publisher Output ==="
cat /tmp/publisher.log || echo "No publisher output"

echo ""
echo "=== Subscriber Output ==="
cat /tmp/subscriber.log || echo "No subscriber output"

echo ""
echo "=== Test Complete ==="

# Cleanup
rm -f /tmp/publisher.log /tmp/subscriber.log