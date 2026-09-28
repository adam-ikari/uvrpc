#!/bin/bash
# UVRPC 快速构建脚本

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${GREEN}=== UVRPC 快速开始 ===${NC}"
echo ""

cd "$(dirname "$0")"

# 检查是否已构建
if [ ! -f "dist/lib/libuvrpc.a" ]; then
    echo -e "${BLUE}1. 构建依赖库...${NC}"
    ./scripts/setup_deps.sh
    echo -e "${BLUE}2. 构建 UVRPC 库...${NC}"
    ./build.sh
    echo -e "${GREEN}✓ UVRPC 库构建完成${NC}"
else
    echo -e "${GREEN}✓ UVRPC 库已存在（跳过构建；重新构建请先 ./build.sh clean）${NC}"
fi

echo ""
echo -e "${BLUE}3. 检查代码生成器...${NC}"
if ! python3 -c "import jinja2" 2>/dev/null; then
    echo -e "${BLUE}   安装生成器依赖: pip install jinja2${NC}"
    pip install jinja2
fi
python3 tools/uvrpcc.py --help >/dev/null 2>&1 && \
    echo -e "${GREEN}✓ 代码生成器可用: python3 tools/uvrpcc.py${NC}"

echo ""
echo -e "${GREEN}=== 构建完成 ===${NC}"
echo ""
echo "下一步："
echo "  1. 编写 schema 文件（参考 schema/rpc.fbs）"
echo "  2. 生成代码: python3 tools/uvrpcc.py schema/your_service.fbs -o generated"
echo "  3. 实现业务逻辑（参考 tools/GENERATOR_QUICK.md）"
echo "  4. 编译并运行（examples/ 下有可运行示例）"
echo ""
echo "查看帮助: python3 tools/uvrpcc.py --help"
echo "快速文档: cat tools/GENERATOR_QUICK.md"
