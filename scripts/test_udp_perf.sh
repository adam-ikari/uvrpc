#!/bin/bash
# UDP transport performance test

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Test parameters
REQUESTS=${1:-100000}
CONCURRENT=1
WARMUP=5000
HOST="127.0.0.1"
PORT=8765

echo -e "${GREEN}=== UDP Transport Performance Test ===${NC}"
echo "Requests: $REQUESTS"
echo "Warmup: $WARMUP"
echo "Address: udp://$HOST:$PORT"
echo ""

# Build project if needed
if [ ! -f "build/examples/simple_client" ]; then
    echo -e "${YELLOW}Building project...${NC}"
    ./build.sh
fi

cd build

# Kill any existing processes
pkill -9 simple_server 2>/dev/null || true
pkill -9 simple_client 2>/dev/null || true
sleep 1

# Start server in background
echo -e "${YELLOW}Starting UDP server...${NC}"
examples/simple_server "udp://$HOST:$PORT" &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Check if server is running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start${NC}"
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"
echo ""

# Run warmup
echo -e "${YELLOW}Running warmup ($WARMUP requests)...${NC}"
examples/simple_client "udp://$HOST:$PORT" $WARMUP > /dev/null 2>&1 || true
sleep 1

# Run performance test
echo -e "${GREEN}Running performance test ($REQUESTS requests)...${NC}"
START_TIME=$(date +%s.%N)
examples/simple_client "udp://$HOST:$PORT" $REQUESTS
EXIT_CODE=$?
END_TIME=$(date +%s.%N)

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}Client failed with exit code $EXIT_CODE${NC}"
    kill $SERVER_PID 2>/dev/null || true
    exit 1
fi

# Calculate metrics
ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
OPS_PER_SEC=$(echo "scale=2; $REQUESTS / $ELAPSED" | bc)

echo ""
echo -e "${GREEN}=== Results ===${NC}"
echo -e "Elapsed Time: ${ELAPSED}s"
echo -e "Operations/sec: ${OPS_PER_SEC} ops/s"
echo -e "Throughput: $(echo "scale=2; $OPS_PER_SEC / 1000" | bc) Kops/s"

# Cleanup
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${GREEN}Test completed successfully${NC}"