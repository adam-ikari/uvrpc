#!/bin/bash
# UVRPC Performance Testing Script
# Tests different configurations to find optimal performance

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
RESULTS_FILE="benchmark_results_$(date +%Y%m%d_%H%M%S).txt"
SERVER_PORT_BASE=5550
DURATION_MS=3000  # 3 seconds per test

echo "======================================" | tee -a "$RESULTS_FILE"
echo "UVRPC Performance Configuration Test" | tee -a "$RESULTS_FILE"
echo "Date: $(date)" | tee -a "$RESULTS_FILE"
echo "======================================" | tee -a "$RESULTS_FILE"
echo "" | tee -a "$RESULTS_FILE"

# Test different transports
TRANSPORTS=(
    "tcp://127.0.0.1:$SERVER_PORT_BASE"
    "udp://127.0.0.1:$SERVER_PORT_BASE"
    "ipc:///tmp/uvrpc_test.ipc"
    "inproc://test"
)

# Test different configurations
declare -a CONFIGS=(
    # Thread mode (shared loop)
    "1,1,0"   # 1 thread, 1 client per thread, not fork
    "1,5,0"   # 1 thread, 5 clients
    "1,10,0"  # 1 thread, 10 clients
    "2,1,0"   # 2 threads, 1 client each
    "2,5,0"   # 2 threads, 5 clients each
    "4,1,0"   # 4 threads, 1 client each
    "4,5,0"   # 4 threads, 5 clients each
    "8,1,0"   # 8 threads, 1 client each
    # Fork mode (separate loops)
    "1,1,1"   # 1 process, 1 client
    "1,5,1"   # 1 process, 5 clients
    "2,1,1"   # 2 processes, 1 client each
    "2,5,1"   # 2 processes, 5 clients each
    "4,1,1"   # 4 processes, 1 client each
    "4,5,1"   # 4 processes, 5 clients each
)

PORT=$SERVER_PORT_BASE

for TRANSPORT in tcp udp ipc inproc; do
    ADDRESS=""
    case $TRANSPORT in
        tcp)
            ADDRESS="tcp://127.0.0.1:$PORT"
            ;;
        udp)
            ADDRESS="udp://127.0.0.1:$PORT"
            ;;
        ipc)
            ADDRESS="ipc:///tmp/uvrpc_${TRANSPORT}.ipc"
            ;;
        inproc)
            ADDRESS="inproc://${TRANSPORT}_test"
            ;;
    esac
    
    echo -e "${GREEN}=====================================${NC}" | tee -a "$RESULTS_FILE"
    echo -e "${GREEN}Testing Transport: $TRANSPORT${NC}" | tee -a "$RESULTS_FILE"
    echo -e "${GREEN}Address: $ADDRESS${NC}" | tee -a "$RESULTS_FILE"
    echo "======================================" | tee -a "$RESULTS_FILE"
    
    for CONFIG in "${CONFIGS[@]}"; do
        IFS=',' read -r THREADS CLIENTS FORK <<< "$CONFIG"
        
        MODE=""
        if [ "$FORK" == "1" ]; then
            MODE="Fork (Multi-Process)"
        else
            MODE="Thread (Shared Loop)"
        fi
        
        echo "" | tee -a "$RESULTS_FILE"
        echo "--- Config: $THREADS $MODE, $CLIENTS clients per $([ "$FORK" == "1" ] && echo "process" || echo "thread") ---" | tee -a "$RESULTS_FILE"
        
        # Start server
        if [ "$TRANSPORT" != "inproc" ]; then
            timeout 10 ./dist/bin/server "$ADDRESS" > /dev/null 2>&1 &
            SERVER_PID=$!
            sleep 1
        fi
        
        # Run client
        CMD="./dist/bin/client -a $ADDRESS -t $THREADS -c $CLIENTS -d $DURATION_MS"
        if [ "$FORK" == "1" ]; then
            CMD="$CMD --fork"
        fi
        
        echo "Running: $CMD" | tee -a "$RESULTS_FILE"
        
        # Run test and capture output
        OUTPUT=$(timeout 15 $CMD 2>&1)
        EXIT_CODE=$?
        
        if [ $EXIT_CODE -eq 0 ]; then
            echo -e "${GREEN}SUCCESS${NC}" | tee -a "$RESULTS_FILE"
            echo "$OUTPUT" | grep -E "Sent|Received|Throughput|Success" | tee -a "$RESULTS_FILE"
        else
            echo -e "${RED}FAILED (exit code: $EXIT_CODE)${NC}" | tee -a "$RESULTS_FILE"
            echo "$OUTPUT" | tail -20 | tee -a "$RESULTS_FILE"
        fi
        
        # Cleanup
        if [ "$TRANSPORT" != "inproc" ] && [ -n "$SERVER_PID" ]; then
            kill $SERVER_PID 2>/dev/null || true
            wait $SERVER_PID 2>/dev/null || true
        fi
        
        sleep 1
        PORT=$((PORT + 1))
    done
    
    echo "" | tee -a "$RESULTS_FILE"
done

echo "" | tee -a "$RESULTS_FILE"
echo "======================================" | tee -a "$RESULTS_FILE"
echo "Testing completed" | tee -a "$RESULTS_FILE"
echo "Results saved to: $RESULTS_FILE" | tee -a "$RESULTS_FILE"
echo "======================================" | tee -a "$RESULTS_FILE"