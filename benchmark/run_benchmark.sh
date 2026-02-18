#!/bin/bash
# UVRPC Unified Benchmark Runner
# This script ensures no processes are left running in the background

set -e  # Exit on error

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SERVER_BIN="${PROJECT_ROOT}/dist/bin/simple_server"
CLIENT_BIN="${PROJECT_ROOT}/dist/bin/simple_client"
PID_DIR="/tmp/uvrpc_benchmark_pids"
SERVER_PID_FILE="${PID_DIR}/server.pid"
CLIENT_PIDS_FILE="${PID_DIR}/clients.pids"

# Default parameters
DEFAULT_ADDRESS="127.0.0.1:5555"
DEFAULT_TEST_TYPE="all"
DEFAULT_DURATION=2000  # 2 seconds in milliseconds
DEFAULT_BATCH=100

# Parse arguments - handle both "address type" and "type" formats
if [ $# -eq 0 ]; then
    ADDRESS="$DEFAULT_ADDRESS"
    TEST_TYPE="$DEFAULT_TEST_TYPE"
elif [ $# -eq 1 ]; then
    # Check if first argument is a transport type (tcp, ipc, udp, inproc) or an address
    if [[ "$1" =~ ^(tcp|ipc|udp|inproc)$ ]]; then
        # It's a transport type, construct address
        ADDRESS="$1://127.0.0.1:5555"
        TEST_TYPE="$DEFAULT_TEST_TYPE"
    elif [[ "$1" =~ ^[a-z]+:// ]] || [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+:[0-9]+$ ]]; then
        ADDRESS="$1"
        TEST_TYPE="$DEFAULT_TEST_TYPE"
    else
        ADDRESS="$DEFAULT_ADDRESS"
        TEST_TYPE="$1"
    fi
elif [ $# -ge 2 ]; then
    # First argument could be transport type or full address
    if [[ "$1" =~ ^(tcp|ipc|udp|inproc)$ ]]; then
        ADDRESS="$1://127.0.0.1:5555"
    else
        ADDRESS="$1"
    fi
    TEST_TYPE="$2"
fi

# Create PID directory
mkdir -p "$PID_DIR"

# Track all child processes
ALL_PIDS=()

# Cleanup function - called on any exit
cleanup() {
    local exit_code=$?
    echo -e "\n${YELLOW}Cleaning up processes...${NC}"
    
    # Kill server if running
    if [ -f "$SERVER_PID_FILE" ]; then
        local server_pid=$(cat "$SERVER_PID_FILE")
        if kill -0 "$server_pid" 2>/dev/null; then
            echo -e "${YELLOW}Stopping server (PID: $server_pid)...${NC}"
            kill -TERM "$server_pid" 2>/dev/null || true
            sleep 1
            if kill -0 "$server_pid" 2>/dev/null; then
                echo -e "${RED}Force killing server...${NC}"
                kill -9 "$server_pid" 2>/dev/null || true
            fi
        fi
        rm -f "$SERVER_PID_FILE"
    fi
    
    # Kill all tracked client processes
    if [ -f "$CLIENT_PIDS_FILE" ]; then
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                echo -e "${YELLOW}Stopping client (PID: $pid)...${NC}"
                kill -TERM "$pid" 2>/dev/null || true
            fi
        done < "$CLIENT_PIDS_FILE"
        sleep 1
        
        # Force kill any remaining clients
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                echo -e "${RED}Force killing client (PID: $pid)...${NC}"
                kill -9 "$pid" 2>/dev/null || true
            fi
        done < "$CLIENT_PIDS_FILE"
        rm -f "$CLIENT_PIDS_FILE"
    fi
    
    # Kill any stray processes (last resort)
    pkill -9 -f "perf_server|perf_benchmark" 2>/dev/null || true
    
    # Cleanup PID directory
    rm -rf "$PID_DIR"
    
    echo -e "${GREEN}Cleanup complete${NC}"
    exit $exit_code
}

# Trap all exit signals
trap cleanup EXIT INT TERM HUP

# Force cleanup of any old processes on startup
force_cleanup_old_processes() {
    echo -e "${YELLOW}Checking for old processes...${NC}"
    
    # Kill any existing servers/benchmarks
    pkill -9 -f "perf_server|perf_benchmark" 2>/dev/null || true
    
    # Wait for processes to die
    sleep 1
    
    # Verify no processes remain
    local remaining=$(pgrep -f "perf_server|perf_benchmark" | wc -l)
    if [ "$remaining" -gt 0 ]; then
        echo -e "${RED}Warning: $remaining old processes still running${NC}"
        pgrep -f "perf_server|perf_benchmark"
        pkill -9 -f "perf_server|perf_benchmark" 2>/dev/null || true
        sleep 1
    fi
}

# Start server with PID tracking
start_server() {
    echo -e "${BLUE}Starting server...${NC}"
    
    # Check if server binary exists
    if [ ! -x "$SERVER_BIN" ]; then
        echo -e "${RED}Error: Server binary not found: $SERVER_BIN${NC}"
        return 1
    fi
    
    # Start server in background
    "$SERVER_BIN" "$ADDRESS" > /tmp/uvrpc_server.log 2>&1 &
    local server_pid=$!
    
    # Save PID
    echo "$server_pid" > "$SERVER_PID_FILE"
    ALL_PIDS+=("$server_pid")
    
    echo -e "${YELLOW}Server PID: $server_pid${NC}"
    
    # Wait for server to be ready
    local port=$(echo "$ADDRESS" | grep -oE '[0-9]+$' || echo "5555")
    for i in {1..30}; do
        if kill -0 "$server_pid" 2>/dev/null && lsof -i ":$port" &>/dev/null; then
            echo -e "${GREEN}Server started successfully${NC}"
            return 0
        fi
        sleep 0.1
    done
    
    echo -e "${RED}Server failed to start or port not listening${NC}"
    cat /tmp/uvrpc_server.log
    return 1
}

# Run benchmark with timeout and PID tracking
run_benchmark_safe() {
    local name="$1"
    shift
    local args=("$@")
    
    echo -e "${BLUE}=== $name ===${NC}"
    
    # Check if client binary exists
    if [ ! -x "$CLIENT_BIN" ]; then
        echo -e "${RED}Error: Client binary not found: $CLIENT_BIN${NC}"
        return 1
    fi
    
    # Run with timeout (30 seconds per test)
    timeout 30 "$CLIENT_BIN" "${args[@]}" 2>&1
    local ret=$?
    
    if [ $ret -eq 124 ]; then
        echo -e "${RED}Test timed out after 30 seconds${NC}"
    elif [ $ret -ne 0 ]; then
        echo -e "${RED}Test failed with exit code: $ret${NC}"
    fi
    
    return $ret
}

run_single() {
    run_benchmark_safe "Single Client Test" \
        "$ADDRESS" -b "$DEFAULT_BATCH" -d "$DEFAULT_DURATION"
}

run_multi() {
    run_benchmark_safe "Multi-Client Test (10 clients)" \
        "$ADDRESS" -c 10 -b "$DEFAULT_BATCH" -d "$DEFAULT_DURATION"
}

run_process() {
    echo -e "${BLUE}=== Multi-Process Test (5 processes) ===${NC}"
    
    local total=0
    local pids=()
    
    for i in {1..5}; do
        local logfile="/tmp/benchmark_client_${i}.log"
        timeout 30 "$CLIENT_BIN" "$ADDRESS" -b "$DEFAULT_BATCH" -d "$DEFAULT_DURATION" > "$logfile" 2>&1 &
        local pid=$!
        pids+=("$pid")
        echo "$pid" >> "$CLIENT_PIDS_FILE"
        ALL_PIDS+=("$pid")
        echo "  Started process $i (PID: $pid)"
    done
    
    # Wait for all processes with timeout
    local deadline=$(($(date +%s) + 35))
    for i in {1..5}; do
        local pid="${pids[$((i-1))]}"
        local remaining=$((deadline - $(date +%s)))
        
        if [ $remaining -gt 0 ]; then
            if ! timeout $remaining wait "$pid" 2>/dev/null; then
                echo -e "${RED}Process $i timed out, killing...${NC}"
                kill -9 "$pid" 2>/dev/null || true
            fi
        else
            echo -e "${RED}Process $i already timed out${NC}"
            kill -9 "$pid" 2>/dev/null || true
        fi
        
        local logfile="/tmp/benchmark_client_${i}.log"
        if [ -f "$logfile" ]; then
            local t=$(grep "Throughput:" "$logfile" | awk '{print $2}' || echo "0")
            echo "  Process $i: ${t} ops/s"
            [ -n "$t" ] && total=$((total + t))
            rm -f "$logfile"
        fi
    done
    
    echo -e "${GREEN}Aggregated: $total ops/s${NC}"
}

run_thread() {
    run_benchmark_safe "Multi-Thread Test (5 threads, 2 clients each)" \
        "$ADDRESS" -t 5 -c 2 -b 50 -d "$DEFAULT_DURATION"
}

run_scaling() {
    echo -e "${BLUE}=== Concurrency Scaling ===${NC}"
    for c in 1 5 10 20; do
        echo -n "  Clients $c: "
        local result=$(timeout 30 "$CLIENT_BIN" -a "$ADDRESS" -c "$c" -b "$DEFAULT_BATCH" -d "$DEFAULT_DURATION" 2>&1 | \
            grep "Throughput:" | awk '{print $2}' || echo "failed")
        echo "$result"
        # Small delay between tests to let server recover
        sleep 0.5
    done
}

run_latency() {
    run_benchmark_safe "Latency Test (1000 iterations)" \
        "$ADDRESS" --latency
}

main() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}UVRPC Unified Benchmark${NC}"
    echo -e "${BLUE}================================${NC}"
    echo -e "Address: $ADDRESS"
    echo -e "Duration: ${DEFAULT_DURATION}ms"
    echo -e "Batch size: $DEFAULT_BATCH"
    echo -e "${BLUE}================================${NC}\n"
    
    # Force cleanup of any old processes
    force_cleanup_old_processes
    
    # Start server
    if ! start_server; then
        echo -e "${RED}Failed to start server, exiting${NC}"
        exit 1
    fi
    
    echo ""
    
    # Run requested tests
    case "$TEST_TYPE" in
        single)
            run_single
            ;;
        multi)
            run_multi
            ;;
        thread)
            run_thread
            ;;
        process)
            run_process
            ;;
        scaling)
            run_scaling
            ;;
        latency)
            run_latency
            ;;
        all)
            run_single
            echo ""
            run_multi
            echo ""
            run_thread
            echo ""
            run_process
            echo ""
            run_latency
            echo ""
            run_scaling
            ;;
        *)
            echo -e "${RED}Usage: $0 [address] [single|multi|thread|process|scaling|latency|all]${NC}"
            echo ""
            echo "Examples:"
            echo "  $0                          # Run all tests"
            echo "  $0 tcp://127.0.0.1:5555 single"
            echo "  $0 tcp://127.0.0.1:5555 all"
            exit 1
            ;;
    esac
    
    echo ""
    echo -e "${GREEN}================================${NC}"
    echo -e "${GREEN}All tests completed${NC}"
    echo -e "${GREEN}================================${NC}"
}

main "$@"
