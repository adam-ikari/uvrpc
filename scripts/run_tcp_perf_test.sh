#!/bin/bash

# UVRPC TCP Performance Test Script

echo "=========================================="
echo "  UVRPC TCP Performance Test Suite"
echo "=========================================="
echo ""

# Kill any existing servers
pkill -f perf_server 2>/dev/null
pkill -f benchmark_multiprocess 2>/dev/null
sleep 1

# Test 1: Basic Throughput Test
echo "Test 1: Basic Throughput Test"
echo "------------------------------"
./dist/bin/perf_server &
SERVER_PID=$!
sleep 2

echo "Running performance client..."
timeout 10 ./dist/bin/perf_client
RESULT=$?

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Test 1 completed"
echo ""

# Test 2: Latency Test
echo "Test 2: Latency Test"
echo "--------------------"
./dist/bin/perf_server &
SERVER_PID=$!
sleep 2

echo "Running latency test..."
timeout 10 ./dist/bin/latency_test
RESULT=$?

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "Test 2 completed"
echo ""

# Cleanup
pkill -f perf_server 2>/dev/null
pkill -f perf_client 2>/dev/null

echo "=========================================="
echo "  All tests completed!"
echo "=========================================="
echo ""
echo "See TCP_PERFORMANCE_RESULTS.md for detailed analysis"
