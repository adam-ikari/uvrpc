#!/bin/bash
# UVRPC Long-term Stability Test

echo "=== UVRPC Long-term Stability Test ==="
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BINARY_DIR="$PROJECT_ROOT/dist/bin"

# Kill any existing processes
pkill -9 -f "server|client" 2>/dev/null
sleep 1

# Test configuration
ADDRESS="tcp://127.0.0.1:5555"
TEST_DURATION=30  # seconds
SAMPLE_INTERVAL=5  # seconds

echo "Starting server..."
"$BINARY_DIR/server" "$ADDRESS" > /tmp/stability_server.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server PID: $SERVER_PID"
echo "Test duration: ${TEST_DURATION}s"
echo "Sample interval: ${SAMPLE_INTERVAL}s"
echo ""
echo "Time(s) | Throughput(ops/s) | Memory(MB)"
echo "--------|-------------------|------------"

START_TIME=$(date +%s)
END_TIME=$((START_TIME + TEST_DURATION))

while [ $(date +%s) -lt $END_TIME ]; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))
    
    # Run a quick throughput test
    THROUGHPUT=$(
        timeout 10 "$BINARY_DIR/client" -a "$ADDRESS" -i 1000 -b 10 2>&1 | \
        grep "Throughput:" | \
        awk '{print $2}' || \
        echo "0"
    )
    
    # Get memory usage
    MEMORY=$(ps -o rss= -p $SERVER_PID 2>/dev/null | awk '{print int($1/1024)}' || echo "0")
    
    printf "   %4d   |     %10s    |     %6d\n" "$ELAPSED" "$THROUGHPUT" "$MEMORY"
    
    sleep $SAMPLE_INTERVAL
done

echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# Cleanup
pkill -9 -f "server|client" 2>/dev/null

echo ""
echo "=== Stability Test Completed ==="
echo ""
echo "Server log summary:"
tail -20 /tmp/stability_server.log