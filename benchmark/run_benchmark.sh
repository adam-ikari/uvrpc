#!/bin/bash
# UVRPC Unified Benchmark Runner

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

ADDRESS="${1:-tcp://127.0.0.1:5555}"
TEST_TYPE="${2:-all}"

cleanup() {
    killall -9 perf_server perf_benchmark 2>/dev/null || true
}

trap cleanup EXIT

start_server() {
    echo -e "${BLUE}Starting server...${NC}"
    /home/zhaodi-chen/project/uvrpc/dist/bin/perf_server > /tmp/uvrpc_server.log 2>&1 &
    # Wait for server to be ready (check if port is listening)
    for i in {1..20}; do
        if lsof -i :5555 &>/dev/null; then
            echo -e "${GREEN}Server started${NC}"
            return 0
        fi
    done
    echo -e "${RED}Server failed to start${NC}"
    return 1
}

run_single() {
    echo -e "${BLUE}=== Single Client Test ===${NC}"
    /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 5000 -b 100 2>&1 | grep -E "Throughput|Success rate"
}

run_multi() {
    echo -e "${BLUE}=== Multi-Client Test ===${NC}"
    /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 5000 -c 10 -b 100 2>&1 | grep -E "Throughput|Success rate"
}

run_process() {
    echo -e "${BLUE}=== Multi-Process Test ===${NC}"
    local total=0
    for i in 1 2 3 4 5; do
        /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 1000 -b 100 > "/tmp/c${i}.log" 2>&1 &
        PIDS[${i}]=$!
    done
    for i in 1 2 3 4 5; do
        wait ${PIDS[${i}]}
        local t=$(grep "Throughput:" "/tmp/c${i}.log" | awk '{print $2}')
        echo "  Process $i: ${t} ops/s"
        [ -n "$t" ] && total=$((total + t))
        rm -f "/tmp/c${i}.log"
    done
    echo -e "${GREEN}Aggregated: $total ops/s${NC}"
}

run_thread() {
    echo -e "${BLUE}=== Multi-Thread Test ===${NC}"
    /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 5000 -t 5 -c 2 -b 50 2>&1 | grep -E "Throughput|Success rate"
}

run_scaling() {
    echo -e "${BLUE}=== Concurrency Scaling ===${NC}"
    for c in 10 50 100 200; do
        echo -n "Clients $c: "
        /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 5000 -c "$c" -b 100 2>&1 | grep "Throughput" | awk '{print $2}'
    done
}

run_latency() {
    echo -e "${BLUE}=== Latency Test ===${NC}"
    /home/zhaodi-chen/project/uvrpc/dist/bin/perf_benchmark "$ADDRESS" -i 1000 --latency 2>&1
}

main() {
    echo -e "${BLUE}=== UVRPC Unified Benchmark ===${NC}"
    start_server
    echo ""
    
    case "$TEST_TYPE" in
        single) run_single ;;
        multi) run_multi ;;
        thread) run_thread ;;
        process) run_process ;;
        scaling) run_scaling ;;
        latency) run_latency ;;
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
        *) echo "Usage: $0 [address] [single|multi|thread|process|scaling|latency|all]" ;;
    esac
    
    echo ""
    echo -e "${GREEN}=== Complete ===${NC}"
}

main "$@"
