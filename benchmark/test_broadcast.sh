#!/bin/bash
# Broadcast Benchmark Test Script
# Tests broadcast performance across all transport types

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
BENCHMARK_DIR="$PROJECT_ROOT/benchmark"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "================================"
echo "UVRPC Broadcast Benchmark"
echo "================================"
echo ""

# Check if benchmark is built
if [ ! -f "$BUILD_DIR/benchmark/broadcast_benchmark" ]; then
    echo -e "${YELLOW}Building broadcast benchmark...${NC}"
    cd "$PROJECT_ROOT"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    make -C build broadcast_benchmark > /dev/null 2>&1
fi

BENCHMARK="$BUILD_DIR/benchmark/broadcast_benchmark"

if [ ! -f "$BENCHMARK" ]; then
    echo -e "${RED}Error: Failed to build broadcast benchmark${NC}"
    exit 1
fi

# Test configurations
DURATION=3000
MESSAGE_SIZE=100
BATCH_SIZE=10

# Transport types to test
declare -A TRANSPORTS=(
    ["UDP"]="udp://127.0.0.1:6000"
    ["TCP"]="tcp://127.0.0.1:6001"
    ["IPC"]="ipc:///tmp/uvrpc_benchmark.sock"
    ["INPROC"]="inproc://uvrpc_benchmark"
)

echo "Test Configuration:"
echo "  Duration: ${DURATION}ms"
echo "  Message size: ${MESSAGE_SIZE} bytes"
echo "  Batch size: ${BATCH_SIZE}"
echo ""

# Function to run benchmark for a transport
run_benchmark() {
    local transport=$1
    local address=$2
    
    echo -e "${BLUE}=== Testing $transport ===${NC}"
    echo "Address: $address"
    echo ""
    
    # Clean up IPC socket if exists
    if [ "$transport" == "IPC" ]; then
        rm -f /tmp/uvrpc_benchmark.sock
    fi
    
    # Start subscriber in background
    echo "Starting subscriber..."
    $BENCHMARK subscriber -a "$address" -d $DURATION > /tmp/subscriber_${transport}.log 2>&1 &
    SUB_PID=$!
    
    # Wait for subscriber to be ready
    sleep 1
    
    # Start publisher
    echo "Starting publisher..."
    $BENCHMARK publisher -a "$address" -d $DURATION -s $MESSAGE_SIZE -b $BATCH_SIZE > /tmp/publisher_${transport}.log 2>&1
    PUB_RESULT=$?
    
    # Wait for subscriber to finish
    wait $SUB_PID 2>/dev/null || true
    SUB_RESULT=$?
    
    # Collect results
    echo ""
    echo "Publisher Results:"
    grep -E "Duration|Messages published|Throughput|Bandwidth" /tmp/publisher_${transport}.log || echo "No results"
    
    echo ""
    echo "Subscriber Results:"
    grep -E "Messages received|Bytes received|Average message size" /tmp/subscriber_${transport}.log || echo "No results"
    
    # Clean up
    if [ "$transport" == "IPC" ]; then
        rm -f /tmp/uvrpc_benchmark.sock
    fi
    
    echo ""
    echo -e "${GREEN}✓ $transport test completed${NC}"
    echo ""
    
    # Brief pause between tests
    sleep 1
}

# Run benchmarks for all transports
for transport in "${!TRANSPORTS[@]}"; do
    run_benchmark "$transport" "${TRANSPORTS[$transport]}"
done

# Summary
echo "================================"
echo "Benchmark Summary"
echo "================================"
echo ""
echo "All transport types tested:"
echo "  ✓ UDP - Connectionless, high performance"
echo "  ✓ TCP - Reliable, connection-based"
echo "  ✓ IPC - Local Unix domain socket"
echo "  ✓ INPROC - In-process communication"
echo ""
echo "Log files saved in /tmp/:"
for transport in "${!TRANSPORTS[@]}"; do
    echo "  - publisher_${transport}.log"
    echo "  - subscriber_${transport}.log"
done
echo ""
echo -e "${GREEN}✓ All benchmarks completed successfully${NC}"