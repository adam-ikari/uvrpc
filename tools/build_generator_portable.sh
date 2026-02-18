#!/bin/bash
# Build UVRPC code generator with glibc bundling using Docker
# This creates a portable executable that works on most Linux systems

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building Portable UVRPC Code Generator ===${NC}"
echo ""
echo "This will build a portable executable using Docker with glibc 2.17"
echo "which is compatible with most Linux distributions (CentOS 7+)"
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker not found${NC}"
    echo "Please install Docker to build the portable executable"
    exit 1
fi

# Create Dockerfile if it doesn't exist
DOCKERFILE="$SCRIPT_DIR/Dockerfile.portable"
if [ ! -f "$DOCKERFILE" ]; then
    echo -e "${YELLOW}Creating Dockerfile for portable build...${NC}"
    cat > "$DOCKERFILE" << 'DOCKEREOF'
FROM centos:7

# Install Python 3.8 and build tools
RUN yum install -y \
    gcc \
    make \
    zlib-devel \
    openssl-devel \
    bzip2-devel \
    readline-devel \
    sqlite-devel \
    wget \
    && yum clean all

# Build and install Python 3.8
RUN cd /tmp && \
    wget https://www.python.org/ftp/python/3.8.18/Python-3.8.18.tgz && \
    tar xzf Python-3.8.18.tgz && \
    cd Python-3.8.18 && \
    ./configure --enable-optimizations --prefix=/usr/local && \
    make -j$(nproc) && \
    make altinstall && \
    rm -rf /tmp/Python-3.8.18*

# Install PyInstaller
RUN /usr/local/bin/python3.8 -m pip install --upgrade pip
RUN /usr/local/bin/python3.8 -m pip install pyinstaller

# Copy generator code
COPY rpc_dsl_generator.py /app/
COPY templates /app/templates/

# Build with PyInstaller
WORKDIR /app
RUN /usr/local/bin/python3.8 -m pyinstaller \
    --onefile \
    --clean \
    --name uvrpc-gen-portable \
    rpc_dsl_generator.py

# Copy the executable to output directory
RUN cp dist/uvrpc-gen-portable /output/

# Display glibc version
RUN ldd --version | head -1

WORKDIR /output
CMD ["bash"]
DOCKEREOF
    echo -e "${GREEN}✓ Dockerfile created${NC}"
fi

# Build Docker image
echo -e "${YELLOW}Building Docker image...${NC}"
docker build -f "$DOCKERFILE" -t uvrpc-gen-portable "$SCRIPT_DIR"

# Extract the executable
echo -e "${YELLOW}Extracting executable...${NC}"
mkdir -p "$SCRIPT_DIR/dist"
docker run --rm -v "$SCRIPT_DIR/dist:/output" uvrpc-gen-portable \
    cp /app/dist/uvrpc-gen-portable /output/uvrpc-gen

# Check glibc requirement
echo ""
echo -e "${BLUE}=== Build Complete ===${NC}"
echo ""
echo "Executable: $SCRIPT_DIR/dist/uvrpc-gen"
echo ""
echo "Checking glibc requirement..."
docker run --rm uvrpc-gen-portable ldd --version | head -1

echo ""
echo -e "${GREEN}✓ Portable executable created${NC}"
echo ""
echo "This executable should work on Linux systems with glibc 2.17 or later"
echo "Compatible with: CentOS 7+, Ubuntu 16.04+, Debian 8+, and most modern Linux"
echo ""
echo "Usage:"
echo "  ./dist/uvrpc-gen --help"
echo "  ./dist/uvrpc-gen --flatcc /path/to/flatcc -o generated schema/service.fbs"
