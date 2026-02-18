#!/bin/bash
# Quick Broadcast Performance Test
# Quick test for broadcast performance with default settings

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BENCHMARK="$PROJECT_ROOT/dist/bin/broadcast_benchmark"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ ! -f "$BENCHMARK" ]; then
    echo -e "${YELLOW}Building broadcast benchmark...${NC}"
    cd "$PROJECT_ROOT"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -C build broadcast_benchmark > /dev/null 2>&1
fi

echo "================================"
echo "Quick Broadcast Performance Test"
echo "================================"
echo ""
echo "Testing UDP broadcast (default settings)"
echo "Duration: 1000ms"
echo ""

# Test UDP broadcast
ADDRESS="udp://127.0.0.1:6000"
DURATION=1000

# Start subscriber
echo "Starting subscriber..."
$BENCHMARK subscriber -a "$ADDRESS" -d $DURATION > /tmp/sub.log 2>&1 &
SUB_PID=$!

# Wait for subscriber to be ready
sleep 0.5

# Start publisher
echo "Starting publisher..."
$BENCHMARK publisher -a "$ADDRESS" -d $DURATION > /tmp/pub.log 2>&1

# Wait for subscriber
wait $SUB_PID 2>/dev/null || true

echo ""
echo "=== Results ==="
echo ""
echo "Publisher:"
grep -E "Duration|Messages published|Throughput|Bandwidth" /tmp/pub.log | tail -4
echo ""
echo "Subscriber:"
grep -E "Messages received|Bytes received" /tmp/sub.log | tail -2

echo ""
echo -e "${GREEN}✓ Test completed${NC}"