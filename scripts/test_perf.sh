#!/bin/bash

# UVRPC 性能测试脚本

echo "=========================================="
echo "  UVRPC 性能测试"
echo "=========================================="
echo ""

# 清理旧进程
pkill -f simple_server 2>/dev/null
pkill -f simple_client 2>/dev/null
sleep 1

# 测试 TCP 性能
echo "测试 1: TCP 性能"
echo "-----------------------------------"

./dist/bin/simple_server tcp://127.0.0.1:5605 2>&1 > /tmp/server.log &
SERVER_PID=$!
sleep 2

START_TIME=$(date +%s.%N)

# 发送 1000 个请求
for i in {1..100}; do
    ./dist/bin/simple_client tcp://127.0.0.1:5605 2>&1 > /tmp/client_$i.log &
done

# 等待所有客户端完成
wait

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)

echo "发送了 100 个并发请求"
echo "总耗时: $DURATION 秒"

THROUGHPUT=$(echo "scale=0; 100 / $DURATION" | bc)
echo "吞吐量: $THROUGHPUT ops/s"

# 统计成功响应
SUCCESS=0
for i in {1..100}; do
    if grep -q "Result: 30" /tmp/client_$i.log 2>/dev/null; then
        ((SUCCESS++))
    fi
done

echo "成功响应: $SUCCESS/100"
echo "成功率: $(echo "scale=1; $SUCCESS * 100 / 100" | bc)%"

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

# 清理日志文件
rm -f /tmp/server.log /tmp/client_*.log

echo ""
echo "=========================================="
echo "  性能测试完成"
echo "=========================================="