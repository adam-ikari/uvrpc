#!/bin/bash

# Test script to reproduce the multi-client timeout issue

set -e

ADDRESS="tcp://127.0.0.1:5555"
ITERATIONS=100

echo "========================================="
echo "UVRPC Multi-Client Test Script"
echo "========================================="
echo ""

# Kill any existing servers
pkill -9 perf_server 2>/dev/null || true
sleep 1

# Start server
echo "Starting server..."
./dist/bin/perf_server $ADDRESS &
SERVER_PID=$!
sleep 2

# Test with different client counts
for num_clients in 1 2 5 10 20 50 100; do
    echo ""
    echo "-----------------------------------------"
    echo "Testing with $num_clients client(s)"
    echo "-----------------------------------------"

    # Run test with timeout
    if timeout 10 ./dist/bin/perf_client_multi $ADDRESS $ITERATIONS $num_clients; then
        echo "✓ Test PASSED with $num_clients clients"
    else
        if [ $? -eq 124 ]; then
            echo "✗ Test TIMEOUT with $num_clients clients"
        else
            echo "✗ Test FAILED with $num_clients clients (exit code: $?)"
        fi
    fi

    sleep 1
done

# Cleanup
echo ""
echo "-----------------------------------------"
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "========================================="
echo "Test script completed"
echo "========================================="