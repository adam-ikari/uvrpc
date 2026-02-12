#!/bin/bash
# Simple multi-process test - run clients sequentially first

ADDR="${1:-tcp://127.0.0.1:12354}"
NUM_CLIENTS="${2:-10}"
REQUESTS_PER_CLIENT="${3:-100}"
PAYLOAD_SIZE="${4:-128}"

echo "Sequential Client Test (NNG)"
echo "============================="
echo "Clients: $NUM_CLIENTS"
echo "Requests per client: $REQUESTS_PER_CLIENT"
echo "Payload: $PAYLOAD_SIZE bytes"
echo ""

# Start server
./build/simple_server "$ADDR" > /tmp/seq_server.log 2>&1 &
SERVER_PID=$!
sleep 2

TOTAL_RECEIVED=0
TOTAL_TIME=0

echo "Running clients sequentially..."
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    echo -n "Client $i: "
    START=$(date +%s%3N)
    
    ./build/simple_multi_client 1 "$REQUESTS_PER_CLIENT" "$PAYLOAD_SIZE" "$ADDR" > "/tmp/seq_client_$i.txt" 2>&1
    
    END=$(date +%s%3N)
    ELAPSED=$((END - START))
    TOTAL_TIME=$((TOTAL_TIME + ELAPSED))
    
    RECEIVED=$(grep "Total received:" "/tmp/seq_client_$i.txt" | awk '{print $3}')
    TOTAL_RECEIVED=$((TOTAL_RECEIVED + RECEIVED))
    
    THROUGHPUT=$((RECEIVED * 1000 / ELAPSED))
    
    echo "$RECEIVED received, ${ELAPSED}ms, $THROUGHPUT ops/s"
done

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Summary:"
echo "  Total received: $TOTAL_RECEIVED / $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"
echo "  Total time: ${TOTAL_TIME}ms"
if [ $TOTAL_TIME -gt 0 ]; then
    AVG_THROUGHPUT=$((TOTAL_RECEIVED * 1000 / TOTAL_TIME))
    echo "  Average throughput: $AVG_THROUGHPUT ops/s"
fi