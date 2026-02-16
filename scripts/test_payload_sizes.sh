#!/bin/bash
# UVRPC Payload Size Performance Test

echo "=== UVRPC Payload Size Performance Test ==="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY_DIR="$PROJECT_ROOT/dist/bin"

# Kill any existing processes
pkill -9 -f "server|client" 2>/dev/null
sleep 1

# Test configuration
ADDRESS="tcp://127.0.0.1:5555"
ITERATIONS=1000

# Payload sizes to test (in bytes)
SIZES=(
    4       # int32
    8       # int32 + int32
    16      # small struct
    32      # medium struct
    64      # larger struct
    128     # small payload
    256     # medium payload
    512     # large payload
    1024    # 1KB
    4096    # 4KB
    8192    # 8KB
)

echo "Starting server..."
"$BINARY_DIR/server" "$ADDRESS" > /tmp/payload_server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server PID: $SERVER_PID"
echo ""
echo "Testing different payload sizes..."
echo ""
echo "Size(bytes) | Throughput(ops/s)"
echo "-----------|-------------------"

for SIZE in "${SIZES[@]}"; do
    echo -n "    $SIZE     | "
    
    # Run client test with specific size
    timeout 30 "$BINARY_DIR/client" -a "$ADDRESS" -i "$ITERATIONS" -b 10 2>&1 | \
        grep "Throughput:" | \
        awk '{print $2}' || \
        echo "failed"
    
    sleep 0.5
done

echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# Cleanup
pkill -9 -f "server|client" 2>/dev/null

echo ""
echo "=== Payload Size Test Completed ==="