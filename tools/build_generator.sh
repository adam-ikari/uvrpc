#!/bin/bash
# Build UVRPC code generator with PyInstaller

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building UVRPC Code Generator ===${NC}"

# Check if PyInstaller is installed
if ! command -v pyinstaller &> /dev/null; then
    echo -e "${YELLOW}PyInstaller not found. Installing...${NC}"
    pip3 install pyinstaller
fi

# Build
cd "$SCRIPT_DIR"
echo -e "${YELLOW}Building with PyInstaller...${NC}"
pyinstaller --clean --onefile rpc_dsl_generator.spec

# Check if build succeeded
if [ -f "dist/uvrpc-gen" ]; then
    echo -e "${GREEN}✓ Build successful!${NC}"
    echo ""
    echo "Executable: $SCRIPT_DIR/dist/uvrpc-gen"
    echo ""
    echo "Usage:"
    echo "  ./dist/uvrpc-gen --help"
    echo "  ./dist/uvrpc-gen --flatcc /path/to/flatcc -o generated schema/service.fbs"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi