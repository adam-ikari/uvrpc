#!/bin/bash
# Final performance test after fix

set -e

echo "=== UVRPC Performance Test (Fixed) ==="
echo ""

# Start server
echo "Starting server..."
./dist/bin/perf_server 127.0.0.1:5555 > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

# Test with different iteration counts
for ITERATIONS in 10 100 1000 10000; do
    echo ""
    echo "Testing with $ITERATIONS iterations..."
    timeout 30 ./dist/bin/perf_client 127.0.0.1:5555 $ITERATIONS 2>&1 | tail -4
done

# Cleanup
kill $SERVER_PID 2>/dev/null
sleep 1

echo ""
echo "=== Test Complete ==="