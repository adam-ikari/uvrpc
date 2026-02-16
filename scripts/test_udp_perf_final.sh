#!/bin/bash
# UDP Performance Test Script

echo "==================================================================="
echo "UVRPC UDP Transport Performance Test"
echo "==================================================================="
echo ""

# Kill any existing processes
pkill -9 simple_server 2>/dev/null
pkill -9 simple_client 2>/dev/null
sleep 1

# Start server
echo "Starting UDP server..."
./dist/bin/simple_server "udp://127.0.0.1:8765" > /tmp/udp_server_perf.log 2>&1 &
SERVER_PID=$!
sleep 3  # Give server more time to start

# Check if server started
if ! ps -p $SERVER_PID > /dev/null; then
    echo "Error: Server failed to start"
    cat /tmp/udp_server_perf.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo ""

# Run client multiple times and measure
echo "Running performance test..."
echo ""

ITERATIONS=5
TOTAL_TIME=0
SUCCESS_COUNT=0

for i in $(seq 1 $ITERATIONS); do
    echo "Test $i/$ITERATIONS..."
    
    START=$(date +%s.%N)
    
    ./dist/bin/simple_client "udp://127.0.0.1:8765" > /tmp/udp_client_${i}.log 2>&1
    CLIENT_RESULT=$?
    
    END=$(date +%s.%N)
    DURATION=$(echo "$END - $START" | bc)
    
    if [ $CLIENT_RESULT -eq 0 ] && grep -q "Result:" /tmp/udp_client_${i}.log; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        TOTAL_TIME=$(echo "$TOTAL_TIME + $DURATION" | bc)
        echo "  ✓ Success (${DURATION}s)"
    else
        echo "  ✗ Failed"
    fi
    
    # Short delay between tests
    sleep 0.5
done

# Cleanup
if ps -p $SERVER_PID > /dev/null; then
    kill $SERVER_PID 2>/dev/null
    sleep 1
fi
pkill -9 simple_server 2>/dev/null
pkill -9 simple_client 2>/dev/null

# Calculate statistics
echo ""
echo "==================================================================="
echo "Test Results"
echo "==================================================================="
echo "Total iterations: $ITERATIONS"
echo "Successful: $SUCCESS_COUNT"
echo "Failed: $((ITERATIONS - SUCCESS_COUNT))"

if [ $SUCCESS_COUNT -gt 0 ]; then
    AVG_TIME=$(echo "scale=4; $TOTAL_TIME / $SUCCESS_COUNT" | bc)
    OPS_PER_SECOND=$(echo "scale=2; 1 / $AVG_TIME" | bc)
    
    echo ""
    echo "Average latency: ${AVG_TIME}s"
    echo "Estimated throughput: ${OPS_PER_SECOND} ops/s"
else
    echo ""
    echo "Error: No successful tests"
    exit 1
fi

echo ""
echo "Note: UDP is connectionless and designed for broadcast/multicast."
echo "Single request latency may vary based on network conditions."
echo ""
echo "For throughput comparison with other transports:"
echo "- IPC: 331,278 ops/s (local process communication)"
echo "- TCP: 87,144 ops/s (network communication)"
echo "- UDP: Variable (depends on use case)"
echo "- INPROC: Highest throughput (same-process)"
echo ""
echo "Test completed successfully"