#!/bin/bash
# Broadcast Performance Test
# Comprehensive broadcast performance test

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK="$PROJECT_ROOT/dist/bin/broadcast_benchmark"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

if [ ! -f "$BENCHMARK" ]; then
    echo -e "${YELLOW}Building broadcast benchmark...${NC}"
    cd "$PROJECT_ROOT"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -C build broadcast_benchmark > /dev/null 2>&1
fi

echo "================================"
echo "Broadcast Performance Test"
echo "================================"
echo ""

# Default settings
ADDRESS=${1:-"udp://127.0.0.1:6000"}
DURATION=${2:-3000}
MESSAGE_SIZE=${3:-100}
BATCH_SIZE=${4:-10}

echo "Configuration:"
echo "  Address: $ADDRESS"
echo "  Duration: ${DURATION}ms"
echo "  Message size: ${MESSAGE_SIZE} bytes"
echo "  Batch size: $BATCH_SIZE"
echo ""

# Clean up IPC socket if needed
if [[ "$ADDRESS" == ipc://* ]]; then
    SOCKET_FILE="${ADDRESS#ipc://}"
    rm -f "$SOCKET_FILE"
fi

# Start subscriber
echo -e "${BLUE}Starting subscriber...${NC}"
$BENCHMARK subscriber -a "$ADDRESS" -d $DURATION > /tmp/subscriber.log 2>&1 &
SUB_PID=$!

# Wait for subscriber to be ready
sleep 1

# Start publisher
echo -e "${BLUE}Starting publisher...${NC}"
$BENCHMARK publisher -a "$ADDRESS" -d $DURATION -s $MESSAGE_SIZE -b $BATCH_SIZE > /tmp/publisher.log 2>&1
PUB_RESULT=$?

# Wait for subscriber to finish
wait $SUB_PID 2>/dev/null || true
SUB_RESULT=$?

echo ""
echo "================================"
echo "Results"
echo "================================"
echo ""

echo "Publisher Metrics:"
echo "-------------------"
grep -E "Duration|Messages published|Bytes sent|Throughput|Bandwidth" /tmp/publisher.log | while read line; do
    echo "  $line"
done

echo ""
echo "Subscriber Metrics:"
echo "-------------------"
grep -E "Messages received|Bytes received|Average message size" /tmp/subscriber.log | while read line; do
    echo "  $line"
done

echo ""
echo "Log files:"
echo "  /tmp/publisher.log"
echo "  /tmp/subscriber.log"

# Clean up
if [[ "$ADDRESS" == ipc://* ]]; then
    rm -f "$SOCKET_FILE"
fi

if [ $PUB_RESULT -eq 0 ] && [ $SUB_RESULT -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ Test completed successfully${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}✗ Test failed${NC}"
    exit 1
fi