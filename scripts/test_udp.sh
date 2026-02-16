#!/bin/bash
# UVRPC UDP Transport Test

echo "=== UVRPC UDP Transport Test ==="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY_DIR="$PROJECT_ROOT/dist/bin"

# Check if binaries exist
if [ ! -f "$BINARY_DIR/server" ] || [ ! -f "$BINARY_DIR/client" ]; then
    echo "Error: Binaries not found. Please build the project first."
    exit 1
fi

# Kill any existing processes
pkill -9 -f "server|client" 2>/dev/null
sleep 1

# Test parameters
ADDRESS="udp://127.0.0.1:5555"
ITERATIONS=100

echo "Starting UDP server..."
"$BINARY_DIR/server" "$ADDRESS" > /tmp/udp_server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server PID: $SERVER_PID"
echo "Running UDP client test..."
echo ""

# Run client test
timeout 30 "$BINARY_DIR/client" -a "$ADDRESS" -i "$ITERATIONS" -b 1 2>&1
CLIENT_EXIT_CODE=$?

echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# Cleanup
pkill -9 -f "server|client" 2>/dev/null

if [ $CLIENT_EXIT_CODE -eq 0 ]; then
    echo ""
    echo "=== UDP Test Completed Successfully ==="
    exit 0
else
    echo ""
    echo "=== UDP Test Failed ==="
    echo "Server log:"
    cat /tmp/udp_server.log
    exit 1
fi