#!/bin/bash

# UVRPC 简单性能测试

echo "=== UVRPC 性能测试 ==="

# 清理现有进程
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null
sleep 1

# 启动服务器
cd /home/zhaodi-chen/project/uvrpc
./dist/bin/simple_server > /tmp/server_perf.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo "错误：服务器启动失败"
    cat /tmp/server_perf.log
    exit 1
fi

echo "服务器已启动 (PID: $SERVER_PID)"

# 测试单个客户端的吞吐量
echo "测试单客户端吞吐量..."
START=$(date +%s)

SUCCESS_COUNT=0
for i in {1..20}; do
    ./dist/bin/simple_client > /tmp/client_${i}.log 2>&1
    if [ $? -eq 0 ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
    echo "进度: $i/20 (成功: $SUCCESS_COUNT)"
done

END=$(date +%s)
DURATION=$((END - START))

echo ""
echo "发送 20 个请求，耗时 $DURATION 秒"
echo "成功: $SUCCESS_COUNT"
if [ $DURATION -gt 0 ]; then
    echo "吞吐量: $((SUCCESS_COUNT / DURATION)) ops/s"
fi

# 清理
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
pkill -9 -f simple_server 2>/dev/null
pkill -9 -f simple_client 2>/dev/null

echo "=== 测试完成 ==="