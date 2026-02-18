#!/bin/bash
# Build UVRPC code generator with bundled FlatCC

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building UVRPC Code Generator with FlatCC ===${NC)"

# Check if FlatCC exists
FLATCC_PATH=""
for path in "$PROJECT_ROOT/build/flatcc/flatcc" "$PROJECT_ROOT/deps/flatcc/bin/flatcc"; do
    if [ -f "$path" ]; then
        FLATCC_PATH="$path"
        break
    fi
done

if [ -z "$FLATCC_PATH" ]; then
    echo -e "${YELLOW}FlatCC not found. Building FlatCC first...${NC}"
    cd "$PROJECT_ROOT"
    make build || {
        echo -e "${RED}Failed to build FlatCC${NC}"
        exit 1
    }
    
    # Find FlatCC after build
    for path in "$PROJECT_ROOT/build/flatcc/flatcc" "$PROJECT_ROOT/deps/flatcc/bin/flatcc"; do
        if [ -f "$path" ]; then
            FLATCC_PATH="$path"
            break
        fi
    done
fi

if [ -z "$FLATCC_PATH" ]; then
    echo -e "${RED}Error: FlatCC not found after build${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Found FlatCC at: $FLATCC_PATH${NC}"

# Check if PyInstaller is installed
if ! command -v pyinstaller &> /dev/null; then
    echo -e "${YELLOW}PyInstaller not found. Installing...${NC}"
    pip3 install pyinstaller
fi

# Build
cd "$SCRIPT_DIR"
echo -e "${YELLOW}Building with PyInstaller and bundled FlatCC...${NC}"
pyinstaller --onefile --clean rpc_dsl_generator_with_flatcc.spec

# Check if build succeeded
if [ -f "dist/uvrpc-gen" ]; then
    echo -e "${GREEN}✓ Build successful!${NC}"
    echo ""
    echo "Executable: $SCRIPT_DIR/dist/uvrpc-gen"
    echo ""
    echo "This executable includes:"
    echo "  - Python runtime"
    echo "  - Jinja2 templates"
    echo "  - FlatCC compiler"
    echo ""
    echo "Usage:"
    echo "  ./dist/uvrpc-gen --help"
    echo "  ./dist/uvrpc-gen -o generated schema/service.fbs"
    echo ""
    echo "Note: No need to specify --flatcc, it's bundled!"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fifi
