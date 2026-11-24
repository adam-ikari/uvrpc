#!/usr/bin/env bash
# Side-by-side UVRPC vs gRPC measurement on ONE host, matched methodology.
#
# Both sides are strict sequential ping-pong: one request in flight, an 8-byte
# echo payload, 1000 warmup requests excluded from timing. UVRPC runs over
# loopback TCP (perf_benchmark tcp, port 16555); gRPC runs over loopback TCP
# with a synchronous unary client (grpc_ping_pong, port 16557). Each is sampled
# SAMPLES times and the median is the comparable number.
#
# Usage: ./benchmark/compare_grpc.sh [requests] [samples]
#   requests: default 100000
#   samples:  default 5
#
# Prerequisites: dist/bin/perf_benchmark (normal build) and
# dist/bin/grpc_ping_pong (build with -DUVRPC_GRPC_BENCHMARK=ON).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REQUESTS="${1:-100000}"
SAMPLES="${2:-5}"
UVRPC_BIN="$ROOT/dist/bin/perf_benchmark"
UVRPC_SYNC_BIN="$ROOT/dist/bin/uvrpc_sync_ping_pong"
GRPC_BIN="$ROOT/dist/bin/grpc_ping_pong"
# Self-contained scratch dir: CI runners do not have this workspace's /tmp layout.
BENCH_TMP="${TMPDIR:-/tmp}/uvrpc-grpc-compare"
mkdir -p "$BENCH_TMP"

for bin in "$UVRPC_BIN" "$UVRPC_SYNC_BIN" "$GRPC_BIN"; do
    if [ ! -x "$bin" ]; then
        echo "missing executable: $bin" >&2
        echo "build perf_benchmark and grpc_ping_pong first (see benchmark/BENCHMARK_README.md)" >&2
        exit 1
    fi
done

# One sample is a noisy observation, not a measurement. Keep all samples so a
# spread shows up instead of being hidden behind a single lucky number.
measure() {
    local bin="$1" label="$2" arg="${3:-}"
    local out lat
    : > "$BENCH_TMP/lat.$label"
    for _ in $(seq 1 "$SAMPLES"); do
        if [ -n "$arg" ]; then
            out=$("$bin" "$REQUESTS" "$arg" 2>/dev/null)
        else
            out=$("$bin" "$REQUESTS" 2>/dev/null)
        fi
        lat=$(printf '%s\n' "$out" | grep -oE '[0-9.]+ us/req' | head -1 | grep -oE '[0-9.]+')
        if [ -z "$lat" ]; then
            echo "no latency parsed from $label: $out" >&2
            exit 1
        fi
        echo "$lat" >> "$BENCH_TMP/lat.$label"
    done
}

measure "$UVRPC_BIN" uvrpc_tcp tcp
measure "$UVRPC_SYNC_BIN" uvrpc_sync
measure "$GRPC_BIN" grpc_tcp

# Peak RSS (VmHWM) is read from /proc while one full run is in progress. It is
# the whole process, so gRPC's worker threads and their stacks are included.
peak_rss_kb() {
    local bin="$1" label="$2" arg="${3:-}"
    "$bin" "$REQUESTS" $arg >/dev/null 2>&1 &
    local pid=$! peak=0 v
    while kill -0 "$pid" 2>/dev/null; do
        v=$(awk '/VmHWM/{print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
        if [ -n "$v" ] && [ "$v" -gt "$peak" ] 2>/dev/null; then peak=$v; fi
        sleep 0.01
    done
    wait "$pid"
    echo "$peak" > "$BENCH_TMP/rss.$label"
}

peak_rss_kb "$UVRPC_BIN" uvrpc_tcp tcp
peak_rss_kb "$UVRPC_SYNC_BIN" uvrpc_sync
peak_rss_kb "$GRPC_BIN" grpc_tcp

median() {
    sort -n "$1" | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}'
}
lowest() { sort -n "$1" | head -1; }
highest() { sort -n "$1" | tail -1; }

row() {
    local label="$1" key="$2" rss="$3"
    local file="$BENCH_TMP/lat.$key"
    local med lo hi thr spread
    med=$(median "$file")
    lo=$(lowest "$file")
    hi=$(highest "$file")
    thr=$(awk -v l="$med" 'BEGIN {printf "%.0f", 1000000/l}')
    spread=$(awk -v lo="$lo" -v hi="$hi" 'BEGIN {printf "%.1fx", hi/lo}')
    printf '| %s | %s us | %s req/s | %s us | %s us | %s | %s KiB |\n' \
        "$label" "$med" "$thr" "$lo" "$hi" "$spread" "$rss"
    if awk -v lo="$lo" -v hi="$hi" 'BEGIN { exit !(hi / lo > 2.0) }'; then
        printf '    WARNING: %s spread %s exceeds 2.0x — host contention; do not quote this run.\n' \
            "$label" "$spread"
    fi
}

samples() {
    local label="$1" key="$2"
    printf '    samples(%s, us): %s\n' "$label" "$(tr '\n' ' ' < "$BENCH_TMP/lat.$key")"
}

echo "## UVRPC vs gRPC (sequential ping-pong)"
echo
echo "Release build, system allocator, debug logging OFF, 8-byte payload, one request in flight."
echo "UVRPC: loopback TCP port 16555 (callback pump); UVRPC_sync: 16558 (blocking await);"
echo "gRPC: loopback TCP port 16557, sync unary client."
echo "Each side sampled $SAMPLES times with $REQUESTS requests per run; median is comparable."
echo
echo "### Measured on"
echo
echo "| | |"
echo "|---|---|"
echo "| Host | $(uname -n) |"
echo "| Kernel | $(uname -sr) |"
echo "| CPU | $(awk -F': ' '/model name/{print $2; exit}' /proc/cpuinfo) |"
echo "| Cores online | $(nproc) |"
echo "| RAM | $(awk '/MemTotal/{printf "%.1f GiB", $2/1048576}' /proc/meminfo) |"
echo "| Load at start | $(awk '{print $1}' /proc/loadavg) |"
echo "| Governor | $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a) |"
echo
echo "### Results"
echo
echo "| Side | Median latency | Median throughput | Best | Worst | Spread | Peak RSS |"
echo "|------|----------------|-------------------|------|-------|--------|----------|"
UVRPC_RSS=$(cat "$BENCH_TMP/rss.uvrpc_tcp")
UVRPC_SYNC_RSS=$(cat "$BENCH_TMP/rss.uvrpc_sync")
GRPC_RSS=$(cat "$BENCH_TMP/rss.grpc_tcp")
row UVRPC_tcp uvrpc_tcp "$UVRPC_RSS"
row UVRPC_sync uvrpc_sync "$UVRPC_SYNC_RSS"
row gRPC_tcp grpc_tcp "$GRPC_RSS"
echo
echo "UVRPC_sync uses the same blocking-unary call style as gRPC (uvrpc_client_call_async)."
echo "Peak RSS is VmHWM of the whole process during a full run (gRPC worker threads included)."
echo "Raw samples:"
samples UVRPC_tcp uvrpc_tcp
samples UVRPC_sync uvrpc_sync
samples gRPC_tcp grpc_tcp
