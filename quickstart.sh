#!/bin/bash
# UVRPC 快速构建脚本

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${GREEN}=== UVRPC 快速开始 ===${NC}"
echo ""

# 检查是否已构建
if [ ! -f "dist/lib/libuvrpc.a" ]; then
    echo -e "${BLUE}1. 构建 UVRPC 库...${NC}"
    make build
    echo -e "${GREEN}✓ UVRPC 库构建完成${NC}"
else
    echo -e "${GREEN}✓ UVRPC 库已存在${NC}"
fi

echo ""
echo -e "${BLUE}2. 构建代码生成器（打包 FlatCC）...${NC}"
make generator-with-flatcc
echo -e "${GREEN}✓ 代码生成器构建完成${NC}"

echo ""
echo -e "${GREEN}=== 构建完成 ===${NC}"
echo ""
echo "下一步："
echo "  1. 编写 schema 文件（参考 schema/rpc_api.fbs）"
echo "  2. 生成代码: ./tools/dist/uvrpc-gen schema/your_service.fbs -o generated"
echo "  3. 实现业务逻辑（参考 tools/GENERATOR_QUICK.md）"
echo "  4. 编译并运行"
echo ""
echo "查看帮助: ./tools/dist/uvrpc-gen --help"
echo "快速文档: cat tools/GENERATOR_QUICK.md"