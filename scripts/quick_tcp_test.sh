#!/bin/bash
# Quick performance test using a single client instance

echo "==================================================================="
echo "UVRPC TCP Quick Performance Test"
echo "==================================================================="
echo ""

# Cleanup
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null
sleep 1

# Start server
echo "Starting server..."
./dist/bin/simple_server > /tmp/server_quick.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "Error: Server failed to start"
    cat /tmp/server_quick.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo ""

# Run a single client and measure
echo "Running test..."
START_TIME=$(date +%s)

# Run client in background and wait for completion
./dist/bin/simple_client > /tmp/client_quick.log 2>&1 &
CLIENT_PID=$!

# Wait for completion (with timeout)
TIMEOUT_COUNT=0
while kill -0 $CLIENT_PID 2>/dev/null; do
    sleep 0.1
    TIMEOUT_COUNT=$((TIMEOUT_COUNT + 1))
    if [ $TIMEOUT_COUNT -gt 100 ]; then
        echo "Timeout waiting for client"
        kill $CLIENT_PID 2>/dev/null
        break
    fi
done

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

# Cleanup
kill $SERVER_PID 2>/dev/null
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null

# Check result
if grep -q "Result: 30" /tmp/client_quick.log; then
    echo ""
    echo "==================================================================="
    echo "Test Results"
    echo "==================================================================="
    echo "Status: SUCCESS"
    echo "Duration: ${DURATION}s"
    echo ""
    echo "Client output:"
    grep -E "(Result|Connected)" /tmp/client_quick.log
    echo ""
    echo "Server output:"
    grep -E "(Received|Result)" /tmp/server_quick.log | tail -5
else
    echo ""
    echo "Test failed"
    echo "Client log:"
    cat /tmp/client_quick.log
fi