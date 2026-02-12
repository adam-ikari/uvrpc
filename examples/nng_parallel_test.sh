#!/bin/bash
# Pure NNG parallel multi-process test

ADDR="${1:-tcp://127.0.0.1:12358}"
NUM_CLIENTS="${2:-10}"
REQUESTS_PER_CLIENT="${3:-100}"
PAYLOAD_SIZE="${4:-128}"

echo "Pure NNG Parallel Multi-Process Test"
echo "====================================="
echo "Clients: $NUM_CLIENTS"
echo "Requests per client: $REQUESTS_PER_CLIENT"
echo "Total requests: $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"
echo "Payload: $PAYLOAD_SIZE bytes"
echo ""

# Start server
./build/simple_server "$ADDR" > /tmp/parallel_nng_server.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 2

# Start all clients in background
echo "Starting $NUM_CLIENTS clients..."
START_TIME=$(date +%s%3N)

PIDS=""
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    ./build/simple_multi_client 1 "$REQUESTS_PER_CLIENT" "$PAYLOAD_SIZE" "$ADDR" > "/tmp/parallel_nng_client_$i.log" 2>&1 &
    PIDS="$PIDS $!"
    echo "  Client $i started with PID $!"
done

# Wait for all clients
echo "Waiting for clients to finish..."
for pid in $PIDS; do
    wait $pid
done

END_TIME=$(date +%s%3N)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "========== Results =========="
echo "Client | Received | Time (ms) | Throughput"
echo "-------|----------|-----------|------------"

TOTAL_RECEIVED=0
SUCCESS_COUNT=0

for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    RECEIVED=$(grep "Total received:" "/tmp/parallel_nng_client_$i.log" 2>/dev/null | awk '{print $3}')
    TIME=$(grep "Time (ms):" "/tmp/parallel_nng_client_$i.log" 2>/dev/null | awk '{print $3}' | tr -d ',')
    
    if [ -n "$RECEIVED" ] && [ -n "$TIME" ]; then
        TOTAL_RECEIVED=$((TOTAL_RECEIVED + RECEIVED))
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        
        if [ "$TIME" != "0" ]; then
            THROUGHPUT=$(echo "scale=0; $RECEIVED * 1000 / $TIME" | bc)
            printf "%6d | %8d | %9s | %11s\n" "$i" "$RECEIVED" "$TIME" "$THROUGHPUT"
        else
            printf "%6d | %8d | %9s | %11s\n" "$i" "$RECEIVED" "$TIME" "N/A"
        fi
    else
        printf "%6d | %8s | %9s | %11s\n" "$i" "FAILED" "N/A" "N/A"
    fi
done

echo ""
echo "Summary:"
echo "  Elapsed time: ${ELAPSED} ms"
echo "  Successful clients: $SUCCESS_COUNT / $NUM_CLIENTS"
echo "  Total received: $TOTAL_RECEIVED / $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"

if [ $ELAPSED -gt 0 ]; then
    THROUGHPUT=$((TOTAL_RECEIVED * 1000 / ELAPSED))
    echo "  Total throughput: $THROUGHPUT ops/s"
fi

echo "============================="

# Cleanup
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "Done."
