#!/bin/bash
# Simple performance test for TCP transport

echo "==================================================================="
echo "UVRPC TCP Transport Performance Test"
echo "==================================================================="
echo ""

# Kill any existing processes
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null
sleep 1

# Start server
echo "Starting server..."
./dist/bin/simple_server > /tmp/server_perf.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server started
if ! ps -p $SERVER_PID > /dev/null; then
    echo "Error: Server failed to start"
    cat /tmp/server_perf.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo ""

# Run client multiple times and measure
echo "Running performance test..."
echo ""

ITERATIONS=20
TOTAL_TIME=0
SUCCESS_COUNT=0

for i in $(seq 1 $ITERATIONS); do
    START=$(date +%s%N)
    
    ./dist/bin/simple_client > /tmp/client_${i}.log 2>&1 &
    CLIENT_PID=$!
    
    # Wait for client to complete or timeout
    wait $CLIENT_PID
    CLIENT_RESULT=$?
    
    END=$(date +%s%N)
    DURATION=$((($END - $START) / 1000000)) # Convert to milliseconds
    
    if [ $CLIENT_RESULT -eq 0 ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        TOTAL_TIME=$((TOTAL_TIME + DURATION))
    fi
    
    # Progress indicator
    if [ $((i % 5)) -eq 0 ]; then
        echo "Progress: $i/$ITERATIONS (Success: $SUCCESS_COUNT)"
    fi
    
    # Cleanup
    pkill -9 -f simple_client 2>/dev/null
    sleep 0.1
done

# Cleanup
kill $SERVER_PID 2>/dev/null
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null

# Calculate statistics
if [ $SUCCESS_COUNT -gt 0 ]; then
    AVG_TIME=$((TOTAL_TIME / SUCCESS_COUNT))
    OPS_PER_SECOND=$((1000 / AVG_TIME))
    
    echo ""
    echo "==================================================================="
    echo "Test Results"
    echo "==================================================================="
    echo "Total iterations: $ITERATIONS"
    echo "Successful: $SUCCESS_COUNT"
    echo "Failed: $((ITERATIONS - SUCCESS_COUNT))"
    echo "Average latency: ${AVG_TIME}ms"
    echo "Throughput: ${OPS_PER_SECOND} ops/s"
    echo ""
    echo "Success rate: $((SUCCESS_COUNT * 100 / ITERATIONS))%"
else
    echo ""
    echo "Error: No successful requests"
fi

echo ""
echo "Logs available in /tmp/"