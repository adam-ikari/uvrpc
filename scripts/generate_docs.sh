#!/bin/bash
# Check and generate Doxygen documentation for UVRPC

set -e

echo "=== UVRPC Doxygen Documentation Generator ==="
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Doxygen is not installed. Installing..."
    # Try to install doxygen
    if command -v apt-get &> /dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y doxygen graphviz
    elif command -v yum &> /dev/null; then
        sudo yum install -y doxygen graphviz
    else
        echo "Please install doxygen manually:"
        echo "  Ubuntu/Debian: sudo apt-get install doxygen graphviz"
        echo "  CentOS/RHEL: sudo yum install doxygen graphviz"
        exit 1
    fi
fi

echo -e "${GREEN}✓ Doxygen found${NC}"
echo ""

# Create output directory
mkdir -p docs/doxygen

# Generate documentation
echo "Generating Doxygen documentation..."
doxygen Doxyfile

# Check if generation was successful
if [ -f "docs/doxygen/html/index.html" ]; then
    echo -e "${GREEN}✓ Documentation generated successfully${NC}"
    echo ""
    echo "Documentation location:"
    echo "  HTML: docs/doxygen/html/index.html"
    echo ""
    echo "Open with:"
    echo "  open docs/doxygen/html/index.html  (macOS)"
    echo "  xdg-open docs/doxygen/html/index.html  (Linux)"
    echo "  start docs/doxygen/html/index.html  (Windows)"
    echo ""
    
    # Show statistics
    echo "=== Documentation Statistics ==="
    if [ -f "docs/doxygen/html/index.html" ]; then
        echo "HTML documentation generated at docs/doxygen/html/"
    fi
else
    echo -e "${YELLOW}⚠ Documentation generation had issues${NC}"
    echo "Check the warnings above for details"
fi

echo ""
echo "=== Doxygen Generation Complete ==="