#!/bin/bash

# UVRPC 简单性能测试

echo "=== UVRPC 性能测试 ==="

# 启动服务器
./dist/bin/simple_server tcp://127.0.0.1:5606 2>&1 &
SERVER_PID=$!
sleep 2

# 测试单个客户端的吞吐量
echo "测试单客户端吞吐量..."
START=$(date +%s)

for i in {1..100}; do
    ./dist/bin/simple_client tcp://127.0.0.1:5606 2>&1 > /dev/null 2>&1 &
done

wait

END=$(date +%s)
DURATION=$((END - START))

echo "发送 100 个请求，耗时 $DURATION 秒"
echo "吞吐量: $((100 / DURATION)) ops/s"

# 清理
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "=== 测试完成 ==="