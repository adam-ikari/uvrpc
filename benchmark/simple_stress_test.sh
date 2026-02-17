#!/bin/bash
# Simple Stress Test - Multiple independent loop clients accessing server

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_BIN="${PROJECT_ROOT}/dist/bin/server"
CLIENT_BIN="${PROJECT_ROOT}/dist/bin/client"

NUM_CLIENTS=${1:-5}
REQUESTS_PER_CLIENT=${2:-1000}
ADDRESS=${3:-tcp://127.0.0.1:6666}

echo -e "${BLUE}=== UVRPC Multi-Loop Stress Test ===${NC}"
echo "Address: $ADDRESS"
echo "Clients: $NUM_CLIENTS"
echo "Requests per Client: $REQUESTS_PER_CLIENT"
echo "Total Requests: $((NUM_CLIENTS * REQUESTS_PER_CLIENT))"
echo -e "${BLUE}=====================================${NC}\n"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    pkill -9 -f "server|client" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Start server
echo "Starting server..."
"$SERVER_BIN" "$ADDRESS" > /tmp/stress_server.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server to start
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    cat /tmp/stress_server.log
    exit 1
fi

echo -e "${GREEN}Server started${NC}\n"

# Run clients sequentially with independent loops
echo "Starting $NUM_CLIENTS clients (each with independent event loop)..."
TOTAL_SENT=0
TOTAL_RECEIVED=0
TOTAL_FAILED=0

for i in $(seq 1 $NUM_CLIENTS); do
    echo -e "${BLUE}Client $i:${NC}"
    
    RESULT=$(timeout 30 "$CLIENT_BIN" -a "$ADDRESS" -b 100 -d 3000 2>&1 | grep -E "Throughput:|Success rate:|Failed:" || echo "Failed")
    
    echo "$RESULT"
    
    # Parse throughput
    THROUGHPUT=$(echo "$RESULT" | grep "Throughput:" | awk '{print $2}' || echo "0")
    SENT=$(echo "$RESULT" | grep "Sent:" | awk '{print $2}' || echo "0")
    RECEIVED=$(echo "$RESULT" | grep "Received:" | awk '{print $2}' || echo "0")
    FAILED=$(echo "$RESULT" | grep "Failed:" | awk '{print $2}' || echo "0")
    
    TOTAL_SENT=$((TOTAL_SENT + SENT))
    TOTAL_RECEIVED=$((TOTAL_RECEIVED + RECEIVED))
    TOTAL_FAILED=$((TOTAL_FAILED + FAILED))
    
    echo ""
    sleep 1
done

# Stop server
echo "Stopping server..."
kill -TERM $SERVER_PID 2>/dev/null || true
sleep 2

if kill -0 $SERVER_PID 2>/dev/null; then
    kill -9 $SERVER_PID 2>/dev/null || true
fi

# Final statistics
echo -e "${GREEN}=== Final Statistics ===${NC}"
echo "Total Requests: $TOTAL_SENT"
echo "Total Received: $TOTAL_RECEIVED"
echo "Total Failed: $TOTAL_FAILED"
SUCCESS_RATE=$(echo "scale=2; $TOTAL_RECEIVED * 100 / $TOTAL_SENT" | bc 2>/dev/null || echo "0")
echo "Success Rate: $SUCCESS_RATE%"
echo -e "${GREEN}=========================${NC}"