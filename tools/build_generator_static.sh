#!/bin/bash
# Build UVRPC code generator with static linking using Alpine Linux
# This creates the smallest possible executable

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building Static UVRPC Code Generator ===${NC}"
echo ""
echo "This will build a statically linked executable using Alpine Linux"
echo "with musl libc for maximum portability and minimal size"
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker not found${NC}"
    echo "Please install Docker to build the static executable"
    exit 1
fi

# Create Dockerfile if it doesn't exist
DOCKERFILE="$SCRIPT_DIR/Dockerfile.static"
if [ ! -f "$DOCKERFILE" ]; then
    echo -e "${YELLOW}Creating Dockerfile for static build...${NC}"
    cat > "$DOCKERFILE" << 'DOCKEREOF'
FROM alpine:3.18

# Install build dependencies
RUN apk add --no-cache \
    python3 \
    py3-pip \
    gcc \
    musl-dev \
    linux-headers

# Install PyInstaller
RUN pip3 install --no-cache pyinstaller

# Copy generator code
COPY rpc_dsl_generator.py /app/
COPY templates /app/templates/

# Build with PyInstaller
WORKDIR /app
RUN pyinstaller \
    --onefile \
    --clean \
    --strip \
    --name uvrpc-gen-static \
    rpc_dsl_generator.py

# Copy the executable to output directory
RUN cp dist/uvrpc-gen-static /output/

# Display information
RUN ls -lh /output/uvrpc-gen-static
RUN file /output/uvrpc-gen-static

WORKDIR /output
CMD ["sh"]
DOCKEREOF
    echo -e "${GREEN}✓ Dockerfile created${NC}"
fi

# Build Docker image
echo -e "${YELLOW}Building Docker image...${NC}"
docker build -f "$DOCKERFILE" -t uvrpc-gen-static "$SCRIPT_DIR"

# Extract the executable
echo -e "${YELLOW}Extracting executable...${NC}"
mkdir -p "$SCRIPT_DIR/dist"
docker run --rm -v "$SCRIPT_DIR/dist:/output" uvrpc-gen-static \
    cp /app/dist/uvrpc-gen-static /output/uvrpc-gen

# Check the executable
echo ""
echo -e "${BLUE}=== Build Complete ===${NC}"
echo ""
echo "Executable: $SCRIPT_DIR/dist/uvrpc-gen"
echo ""
echo "Checking executable info..."
docker run --rm uvrpc-gen-static ls -lh /app/dist/uvrpc-gen-static
docker run --rm uvrpc-gen-static file /app/dist/uvrpc-gen-static

echo ""
echo -e "${GREEN}✓ Static executable created${NC}"
echo ""
echo "This executable is statically linked with musl libc"
echo "Compatible with almost all Linux distributions"
echo "Size should be ~10-15MB"
echo ""
echo "Usage:"
echo "  ./dist/uvrpc-gen --help"
echo "  ./dist/uvrpc-gen --flatcc /path/to/flatcc -o generated schema/service.fbs"