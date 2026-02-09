#!/bin/bash
set -e

echo "======================================"
echo "  Running uvrpc E2E Tests"
echo "======================================"
echo ""

# 检查可执行文件
if [ ! -f "build/test_basic" ]; then
    echo "Error: test_basic not found. Run: cmake .. && make"
    exit 1
fi

if [ ! -f "build/test_router_dealer" ]; then
    echo "Error: test_router_dealer not found. Run: cmake .. && make"
    exit 1
fi

# 运行基础测试
echo "Running basic tests..."
./build/test_basic
BASIC_RESULT=$?

# 运行 ROUTER_DEALER 测试
echo ""
echo "Running ROUTER_DEALER tests..."
./build/test_router_dealer
ROUTER_RESULT=$?

# 汇总结果
echo ""
echo "======================================"
echo "  Test Results Summary"
echo "======================================"

if [ $BASIC_RESULT -eq 0 ] && [ $ROUTER_RESULT -eq 0 ]; then
    echo "✅ All test suites passed"
    exit 0
else
    echo "❌ Some test suites failed"
    exit 1
fi