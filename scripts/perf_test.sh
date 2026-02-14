#!/bin/bash
# UVRPC Performance Test Script

set -e

# Configuration
ADDRESS="tcp://127.0.0.1:7777"
METHOD="echo"
REQUESTS=1000
TIMEOUT=10

echo "=== UVRPC Performance Test ==="
echo "Address: $ADDRESS"
echo "Requests: $REQUESTS"
echo "Method: $METHOD"
echo ""

# Start server
echo "Starting server..."
./dist/bin/simple_server $ADDRESS > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

# Wait for server to be ready
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start"
    cat /tmp/srv.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo ""

# Run benchmark
echo "Running $REQUESTS requests..."
START_TIME=$(date +%s)

SUCCESS=0
FAILED=0

for i in $(seq 1 $REQUESTS); do
    if ./dist/bin/simple_client $ADDRESS $METHOD 2>&1 | grep -q "Received response"; then
        SUCCESS=$((SUCCESS + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    
    # Print progress every 100 requests
    if [ $((i % 100)) -eq 0 ]; then
        echo "Progress: $i/$REQUESTS requests"
    fi
done

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

# Cleanup
kill $SERVER_PID 2>/dev/null
sleep 1

# Calculate throughput
if [ $DURATION -gt 0 ]; then
    THROUGHPUT=$((REQUESTS / DURATION))
else
    THROUGHPUT="N/A"
fi

echo ""
echo "=== Test Results ==="
echo "Total requests: $REQUESTS"
echo "Successful: $SUCCESS"
echo "Failed: $FAILED"
echo "Duration: ${DURATION}s"
echo "Throughput: ${THROUGHPUT} requests/second"
echo "Success rate: $(echo "scale=2; $SUCCESS * 100 / $REQUESTS" | bc)%"
echo "===================="

# Show last few server logs
echo ""
echo "Server logs (last 10 lines):"
tail -10 /tmp/srv.log