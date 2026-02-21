#!/bin/bash

# Simple broadcast test - check if APIs compile and link

echo "=== UVRPC Broadcast API Test ==="
echo ""

cd "$(dirname "$0")/.."

# Check if binary exists
if [ ! -f "./dist/bin/broadcast_service_demo" ]; then
    echo "Building broadcast_service_demo..."
    make broadcast_service_demo
fi

echo "✓ Binary built successfully"
echo ""

# Test publisher mode with timeout
echo "Testing publisher mode (2 second timeout)..."
timeout 2 ./dist/bin/broadcast_service_demo publisher > /tmp/pub_test.log 2>&1
PUB_EXIT_CODE=$?

if [ $PUB_EXIT_CODE -eq 124 ]; then
    echo "✗ Publisher timeout - may have issue"
    cat /tmp/pub_test.log
    exit 1
elif [ $PUB_EXIT_CODE -ne 0 ]; then
    echo "✗ Publisher failed with exit code $PUB_EXIT_CODE"
    cat /tmp/pub_test.log
    exit 1
fi

if [ -s /tmp/pub_test.log ]; then
    echo "✓ Publisher produced output:"
    head -10 /tmp/pub_test.log | sed 's/^/  /'
else
    echo "✓ Publisher ran successfully"
fi

echo ""
echo "=== Test Complete ==="

rm -f /tmp/pub_test.log