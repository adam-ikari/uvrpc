#!/bin/bash
# Multi-process client test for UVRPC
# Each client runs in a separate process

ADDR="${1:-tcp://127.0.0.1:12351}"
NUM_CLIENTS="${2:-10}"
REQUESTS_PER_CLIENT="${3:-1000}"
PAYLOAD_SIZE="${4:-128}"

echo "UVRPC Multi-Process Concurrent Test"
echo "====================================="
echo "Clients (processes): $NUM_CLIENTS"
echo "Requests per client: $REQUESTS_PER_CLIENT"
echo "Total requests: $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"
echo "Payload: $PAYLOAD_SIZE bytes"
echo "Server: $ADDR"
echo ""

# Start server
./build/echo_server "$ADDR" > /tmp/uvrpc_multi_proc_server.log 2>&1 &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Create results directory
mkdir -p /tmp/uvrpc_multi_results

# Start clients in parallel
echo "Starting $NUM_CLIENTS client processes..."
START_TIME=$(date +%s%3N)

for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    (
        ./build/echo_client "$ADDR" "$REQUESTS_PER_CLIENT" "$PAYLOAD_SIZE" > "/tmp/uvrpc_multi_results/client_$i.txt" 2>&1
        echo $? > "/tmp/uvrpc_multi_results/client_${i}_exit_code.txt"
    ) &
done

# Wait for all clients to finish
wait

END_TIME=$(date +%s%3N)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "========== Results =========="
echo "Client | Exit Code | Status"
echo "-------|-----------|--------"

TOTAL_RECEIVED=0
TOTAL_SUCCESS=0

for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    EXIT_CODE=$(cat "/tmp/uvrpc_multi_results/client_${i}_exit_code.txt" 2>/dev/null || echo "-1")
    
    if [ "$EXIT_CODE" = "0" ]; then
        STATUS="OK"
        TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
        
        # Parse received count from output
        RECEIVED=$(grep "Received:" "/tmp/uvrpc_multi_results/client_$i.txt" | awk '{print $2}')
        if [ -n "$RECEIVED" ]; then
            TOTAL_RECEIVED=$((TOTAL_RECEIVED + RECEIVED))
        fi
    else
        STATUS="FAILED"
    fi
    
    printf "%6d | %9d | %s\n" "$i" "$EXIT_CODE" "$STATUS"
done

echo ""
echo "Summary:"
echo "  Elapsed time: ${ELAPSED} ms"
echo "  Successful clients: $TOTAL_SUCCESS / $NUM_CLIENTS"
echo "  Total received: $TOTAL_RECEIVED / $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"

if [ $ELAPSED -gt 0 ]; then
    THROUGHPUT=$((TOTAL_RECEIVED * 1000 / ELAPSED))
    echo "  Total throughput: $THROUGHPUT ops/s"
fi

echo "============================="

# Cleanup
echo "Cleaning up..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# Show server log if there were errors
if [ $TOTAL_SUCCESS -lt $NUM_CLIENTS ]; then
    echo ""
    echo "Server log:"
    cat /tmp/uvrpc_multi_proc_server.log
fi

echo "Done."
