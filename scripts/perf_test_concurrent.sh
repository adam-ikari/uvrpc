#!/bin/bash
# UVRPC 服务器处理能力测试

set -e

ADDRESS="tcp://127.0.0.1:9999"
DURATION=5

echo "=== UVRPC Server Processing Capacity ==="
echo "Testing for ${DURATION} seconds"
echo ""

# Start server
./dist/bin/simple_server $ADDRESS > /tmp/srv.log 2>&1 &
SERVER_PID=$!
sleep 2

# Run multiple clients in parallel
echo "Starting 10 concurrent clients..."
for i in {1..10}; do
    (
        START=$(date +%s)
        END=$((START + DURATION))
        COUNT=0
        
        while [ $(date +%s) -lt $END ]; do
            if ./dist/bin/simple_client $ADDRESS echo 2>&1 | grep -q "Received response"; then
                COUNT=$((COUNT + 1))
            fi
        done
        
        echo "Client $i: $COUNT requests in ${DURATION}s"
    ) &
done

# Wait for all clients
wait

# Cleanup
kill $SERVER_PID 2>/dev/null
sleep 1

# Analyze server logs
HANDLED=$(tail -500 /tmp/srv.log | grep -c "Looking for handler")
echo ""
echo "Server handled: $HANDLED requests in ${DURATION}s"
echo "Average throughput: $((HANDLED / DURATION)) RPS"