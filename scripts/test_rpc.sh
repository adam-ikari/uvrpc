#!/bin/bash

# UVRPC RPC 功能测试脚本

echo "=========================================="
echo "  UVRPC RPC 功能测试"
echo "=========================================="
echo ""

# 清理旧进程
pkill -f simple_server 2>/dev/null
pkill -f simple_client 2>/dev/null
sleep 1

# 测试 1: TCP RPC
echo "测试 1: TCP RPC"
echo "-----------------------------------"

./dist/bin/simple_server tcp://127.0.0.1:5600 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ 服务器启动失败"
    exit 1
fi

echo "✓ 服务器启动成功 (PID: $SERVER_PID)"

timeout 5 ./dist/bin/simple_client tcp://127.0.0.1:5600 2>&1 | grep -E "(Result|exit)"
CLIENT_RESULT=$?

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

if [ $CLIENT_RESULT -eq 0 ]; then
    echo "✓ TCP RPC 测试通过"
else
    echo "❌ TCP RPC 测试失败"
fi

echo ""

# 测试 2: IPC RPC
echo "测试 2: IPC RPC"
echo "-----------------------------------"

rm -f /tmp/uvrpc_test.sock
./dist/bin/simple_server ipc:///tmp/uvrpc_test.sock 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ 服务器启动失败"
    exit 1
fi

echo "✓ 服务器启动成功 (PID: $SERVER_PID)"

timeout 5 ./dist/bin/simple_client ipc:///tmp/uvrpc_test.sock 2>&1 | grep -E "(Result|exit)"
CLIENT_RESULT=$?

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm -f /tmp/uvrpc_test.sock

if [ $CLIENT_RESULT -eq 0 ]; then
    echo "✓ IPC RPC 测试通过"
else
    echo "❌ IPC RPC 测试失败"
fi

echo ""

# 测试 3: UDP RPC
echo "测试 3: UDP RPC"
echo "-----------------------------------"

./dist/bin/udp_rpc_demo server udp://127.0.0.1:5601 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ 服务器启动失败"
    exit 1
fi

echo "✓ 服务器启动成功 (PID: $SERVER_PID)"

timeout 5 ./dist/bin/udp_rpc_demo client udp://127.0.0.1:5601 2>&1 | grep -E "(Result|exit)"
CLIENT_RESULT=$?

kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

if [ $CLIENT_RESULT -eq 0 ]; then
    echo "✓ UDP RPC 测试通过"
else
    echo "❌ UDP RPC 测试失败"
fi

echo ""

# 最终清理
pkill -f simple_server 2>/dev/null
pkill -f udp_rpc_demo 2>/dev/null

echo "=========================================="
echo "  测试完成"
echo "=========================================="