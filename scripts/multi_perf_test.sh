#!/bin/bash
# Multiple performance test runs

echo "=== UVRPC Multiple Performance Tests ==="

TOTAL_THROUGHPUT=0
RUNS=5

for i in $(seq 1 $RUNS); do
    echo ""
    echo "Run $i/$RUNS..."

    # Start server
    ./dist/bin/server tcp://127.0.0.1:5555 > /tmp/server.log 2>&1 &
    SERVER_PID=$!
    sleep 2

    # Run client test
    RESULT=$(timeout 5 ./dist/bin/client tcp://127.0.0.1:5555 -d 1000 2>&1 | grep "Throughput:" | head -1 | awk '{print $2}')

    # Kill server
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true

    echo "  Throughput: $RESULT ops/s"
    if [ -n "$RESULT" ]; then
        TOTAL_THROUGHPUT=$((TOTAL_THROUGHPUT + RESULT))
    fi
done

AVG_THROUGHPUT=$((TOTAL_THROUGHPUT / RUNS))

echo ""
echo "=== Summary ==="
echo "Total runs: $RUNS"
echo "Average throughput: $AVG_THROUGHPUT ops/s"
echo "Test completed"