#!/bin/bash
# 一键示例：从 schema 到可运行程序

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== UVRPC 一键示例 ===${NC}"
echo ""

# 检查生成器
if [ ! -f "tools/dist/uvrpc-gen" ]; then
    echo -e "${YELLOW}代码生成器未找到，正在构建...${NC}"
    make generator-with-flatcc
fi

# 清理旧的生成文件
echo -e "${BLUE}清理旧的生成文件...${NC}"
rm -rf generated/rpc_example*

# 生成代码
echo -e "${BLUE}生成代码...${NC}"
./tools/dist/uvrpc-gen schema/rpc_example.fbs -o generated

echo -e "${GREEN}✓ 代码生成完成${NC}"
echo ""

# 编译示例
echo -e "${BLUE}编译示例程序...${NC}"
gcc -o dist/bin/example_server \
    generated/rpc_math_server_stub.c \
    generated/rpc_math_rpc_common.c \
    generated/rpc_builder.c \
    generated/rpc_reader.c \
    examples/rpc_user_impl.c \
    -Igenerated \
    -Iinclude \
    -Ldist/lib \
    -luvrpc -luv -lmimalloc \
    -pthread

gcc -o dist/bin/example_client \
    generated/rpc_math_client.c \
    generated/rpc_math_rpc_common.c \
    generated/rpc_builder.c \
    generated/rpc_reader.c \
    -Igenerated \
    -Iinclude \
    -Ldist/lib \
    -luvrpc -luv -lmimalloc \
    -pthread

echo -e "${GREEN}✓ 编译完成${NC}"
echo ""

echo -e "${GREEN}=== 示例程序已就绪 ===${NC}"
echo ""
echo "终端 1 - 运行服务端："
echo "  ./dist/bin/example_server"
echo ""
echo "终端 2 - 运行客户端："
echo "  ./dist/bin/example_client"
echo ""
echo "生成的文件位置："
echo "  - generated/rpc_math_api.h"
echo "  - generated/rpc_math_server_stub.c"
echo "  - generated/rpc_math_client.c"
echo "  - generated/rpc_math_rpc_common.c"
echo "  - generated/rpc_builder.h"
echo "  - generated/rpc_reader.h"
echo ""
echo "查看完整文档："
echo "  cat tools/GENERATOR_QUICK.md"