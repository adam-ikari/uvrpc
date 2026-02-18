#!/bin/bash
set -e
BENCHMARK="./dist/bin/perf_benchmark"
OUT="benchmark/results/quick_report.md"
mkdir -p benchmark/results

cleanup() { pkill -9 perf_benchmark 2>/dev/null || true; rm -f /tmp/uvrpc*.sock; }
trap cleanup EXIT
cleanup

echo "# UVRPC Performance Test Results" > "$OUT"
echo "**Date:** $(date)" >> "$OUT"
echo "" >> "$OUT"
echo "## CS Mode" >> "$OUT"
echo "| Transport | Clients | Sent | Received | Throughput |" >> "$OUT"
echo "|-----------|---------|------|----------|------------|" >> "$OUT"

run_test() {
    local t="$1" addr="$2" params="$3"
    echo "Test: $t"
    "$BENCHMARK" --server -a "$addr" --server-timeout 5000 >/dev/null 2>&1 &
    SP=$!; sleep 2
    if ! kill -0 $SP 2>/dev/null; then echo "  FAIL"; return; fi
    O=$(timeout 20 "$BENCHMARK" -a "$addr" $params -d 2000 2>&1)
    kill $SP 2>/dev/null; wait $SP 2>/dev/null
    S=$(echo "$O" | grep "^Sent:" | awk '{print $2}' || echo "0")
    R=$(echo "$O" | grep "^Received:" | awk '{print $2}' || echo "0")
    T=$(echo "$O" | grep "Client throughput:" | awk '{print $3}' || echo "0")
    echo "  ✓ S:$S R:$R T:$T"
    echo "| $t | 5 | $S | $R | $T |" >> "$OUT"
    sleep 1
}

run_test "TCP" "tcp://127.0.0.1:5555" "-c 5 -b 100"
run_test "UDP" "udp://127.0.0.1:6000" "-c 5 -b 100"
run_test "IPC" "ipc:///tmp/uvrpc1.sock" "-c 5 -b 100"
run_test "INPROC" "inproc://test" "-c 5 -b 100"

echo "" >> "$OUT"
echo "## Broadcast Mode" >> "$OUT"
echo "| Transport | Messages | Throughput |" >> "$OUT"
echo "|-----------|----------|------------|" >> "$OUT"

run_bc() {
    local t="$1" addr="$2"
    echo "Test: $t"
    "$BENCHMARK" --publisher -a "$addr" -p 1 -s 1 -b 50 -d 2000 >/dev/null 2>&1 &
    PP=$!; sleep 1
    if ! kill -0 $PP 2>/dev/null; then echo "  FAIL"; return; fi
    O=$(timeout 20 "$BENCHMARK" --subscriber -a "$addr" -s 1 -b 50 -d 2000 2>&1)
    kill $PP 2>/dev/null; wait $PP 2>/dev/null
    M=$(echo "$O" | grep "Messages received:" | awk '{print $3}' || echo "0")
    T=$(echo "$O" | grep "Throughput:" | awk '{print $2}' || echo "0")
    echo "  ✓ M:$M T:$T"
    echo "| $t | $M | $T |" >> "$OUT"
    sleep 1
}

run_bc "UDP" "udp://127.0.0.1:7000"
run_bc "IPC" "ipc:///tmp/uvrpcb.sock"

echo "" >> "$OUT"
echo "## Latency" >> "$OUT"
echo "| Transport | Avg (ms) |" >> "$OUT"
echo "|-----------|----------|" >> "$OUT"

run_lat() {
    local t="$1" addr="$2"
    echo "Test: $t"
    "$BENCHMARK" --server -a "$addr" --server-timeout 6000 >/dev/null 2>&1 &
    SP=$!; sleep 2
    if ! kill -0 $SP 2>/dev/null; then echo "  FAIL"; return; fi
    O=$(timeout 20 "$BENCHMARK" -a "$addr" --latency 2>&1)
    kill $SP 2>/dev/null; wait $SP 2>/dev/null
    A=$(echo "$O" | grep "Avg:" | awk '{print $2}' || echo "N/A")
    echo "  ✓ Avg:$A ms"
    echo "| $t | $A |" >> "$OUT"
    sleep 1
}

run_lat "TCP" "tcp://127.0.0.1:8000"
run_lat "UDP" "udp://127.0.0.1:8001"
run_lat "IPC" "ipc:///tmp/uvrpclat.sock"

echo "" >> "$OUT"
echo "## Summary" >> "$OUT"
echo "All tests completed. See results above." >> "$OUT"
echo "Report saved to: $OUT"
cat "$OUT"
