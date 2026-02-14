#!/bin/bash

# Latency comparison script for UVRPC performance modes

cleanup() {
    pkill -9 perf_server latency_test 2>/dev/null
}

trap cleanup EXIT

echo "======================================"
echo "UVRPC Latency Comparison Test"
echo "======================================"
echo ""

# Start server
./dist/bin/perf_server > /tmp/perf_server.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: Server failed to start"
    cat /tmp/perf_server.log
    exit 1
fi

echo "=== Low Latency Mode ==="
echo "Testing single-request latency..."
LL_TOTAL=0
LL_COUNT=0
for i in {1..10}; do
    OUTPUT=$(timeout 5 ./dist/bin/latency_test 127.0.0.1:5555 low_latency 2>&1)
    if echo "$OUTPUT" | grep -q "Latency:"; then
        LATENCY=$(echo "$OUTPUT" | grep "Latency:" | awk '{print $2}')
        LL_TOTAL=$(echo "$LL_TOTAL + $LATENCY" | bc)
        LL_COUNT=$((LL_COUNT + 1))
        echo "  Run $i: $LATENCY us"
    fi
done

if [ $LL_COUNT -gt 0 ]; then
    LL_AVG=$(echo "scale=3; $LL_TOTAL / $LL_COUNT" | bc)
    echo "  Average: $LL_AVG us"
else
    echo "  ERROR: No successful measurements"
fi

echo ""
echo "=== High Throughput Mode ==="
echo "Testing single-request latency..."
HT_TOTAL=0
HT_COUNT=0
for i in {1..10}; do
    OUTPUT=$(timeout 5 ./dist/bin/latency_test 127.0.0.1:5555 high_throughput 2>&1)
    if echo "$OUTPUT" | grep -q "Latency:"; then
        LATENCY=$(echo "$OUTPUT" | grep "Latency:" | awk '{print $2}')
        HT_TOTAL=$(echo "$HT_TOTAL + $LATENCY" | bc)
        HT_COUNT=$((HT_COUNT + 1))
        echo "  Run $i: $LATENCY us"
    fi
done

if [ $HT_COUNT -gt 0 ]; then
    HT_AVG=$(echo "scale=3; $HT_TOTAL / $HT_COUNT" | bc)
    echo "  Average: $HT_AVG us"
else
    echo "  ERROR: No successful measurements"
fi

echo ""
echo "======================================"
echo "Summary:"
echo "======================================"
if [ $LL_COUNT -gt 0 ] && [ $HT_COUNT -gt 0 ]; then
    echo "Low Latency Mode:    $LL_AVG us average"
    echo "High Throughput Mode: $HT_AVG us average"
    DIFF=$(echo "scale=3; $LL_AVG - $HT_AVG" | bc)
    if [ $(echo "$DIFF < 0" | bc) -eq 1 ]; then
        DIFF=$(echo "scale=3; -$DIFF" | bc)
        echo "Low Latency is $DIFF us faster"
    else
        echo "High Throughput is $DIFF us faster"
    fi
else
    echo "ERROR: Could not complete comparison"
fi
echo "======================================"