#!/bin/bash
# Build VitePress documentation site

set -e

cd "$(dirname "$0")"

echo "=== Building UVRPC Documentation ==="

# Clean previous build
echo "Cleaning previous build..."
rm -rf .vitepress/dist

# Build documentation
echo "Building documentation..."
npm run docs:build

echo ""
echo "=== Build Complete ==="
echo "Documentation built in: .vitepress/dist"
echo ""
echo "To preview:"
echo "  npm run docs:preview"
echo ""
echo "To deploy:"
echo "  Copy .vitepress/dist to your web server"