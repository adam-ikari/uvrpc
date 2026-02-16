#!/bin/bash
# Performance test script for all transport types

echo "==================================================================="
echo "UVRPC Transport Layer Performance Test"
echo "==================================================================="
echo ""

BUILD_DIR="../build"
BINARY_DIR="../dist/bin"

# Function to run test and capture metrics
run_test() {
    local name=$1
    local transport=$2
    local iterations=${3:-1000}
    
    echo "Testing: $name"
    echo "Transport: $transport"
    echo "Iterations: $iterations"
    echo "----------------------------------------"
    
    # Kill any existing processes
    pkill -9 -f simple_server 2>/dev/null
    pkill -9 -f simple_client 2>/dev/null
    sleep 1
    
    # Start server
    timeout 60 $BINARY_DIR/simple_server > /tmp/server_${name}.log 2>&1 &
    SERVER_PID=$!
    sleep 2
    
    # Run client and measure time
    local start_time=$(date +%s%N)
    
    for ((i=0; i<iterations; i++)); do
        timeout 3 $BINARY_DIR/simple_client > /tmp/client_${name}_${i}.log 2>&1
        if [ $? -ne 0 ]; then
            echo "Error in iteration $i"
            break
        fi
    done
    
    local end_time=$(date +%s%N)
    local duration=$((($end_time - $start_time) / 1000000)) # Convert to milliseconds
    local ops_per_second=$((($iterations * 1000) / $duration))
    
    echo "Total time: ${duration}ms"
    echo "Throughput: ${ops_per_second} ops/s"
    echo ""
    
    # Cleanup
    kill $SERVER_PID 2>/dev/null
    pkill -9 -f simple_server 2>/dev/null
    pkill -9 -f simple_client 2>/dev/null
    
    # Report
    echo "$name,${ops_per_second},${duration}" >> /tmp/perf_results.csv
}

# Check if binaries exist
if [ ! -f "$BINARY_DIR/simple_server" ] || [ ! -f "$BINARY_DIR/simple_client" ]; then
    echo "Error: Binaries not found. Please build the project first."
    exit 1
fi

# Initialize results file
echo "Transport,Throughput(ops/s),Duration(ms)" > /tmp/perf_results.csv

# Note: simple_server/simple_client currently only support TCP
# This script shows the pattern for testing different transports
# When other transport examples are available, they should be tested here

run_test "TCP (localhost)" "tcp://127.0.0.1:5555" 100

echo "==================================================================="
echo "Performance Test Complete"
echo "==================================================================="
echo ""
echo "Results:"
cat /tmp/perf_results.csv
echo ""
echo "Detailed logs available in /tmp/"