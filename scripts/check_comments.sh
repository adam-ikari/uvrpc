#!/bin/bash
# Translate Chinese comments to English in UVRPC source code

set -e

echo "=== UVRPC Comment Translation Tool ==="
echo ""

# Files to check
FILES=(
    "include/uvbus_v2.h"
    "include/uvrpc_allocator.h"
)

echo "Checking for Chinese comments in:"
for file in "${FILES[@]}"; do
    echo "  - $file"
done
echo ""

# Check if doxygen is available
if command -v doxygen &> /dev/null; then
    echo "Doxygen is available"
    DOXYGEN_AVAILABLE=true
else
    echo "Doxygen is not available (comment translation will continue)"
    DOXYGEN_AVAILABLE=false
fi
echo ""

# Count Chinese comments
echo "=== Chinese Comment Statistics ==="
for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        COUNT=$(grep -c "[\u4e00-\u9fff]" "$file" || echo "0")
        echo "$file: $COUNT Chinese comments"
    fi
done
echo ""

# Provide translation guidance
echo "=== Translation Guidelines ==="
echo ""
echo "Files with Chinese comments found:"
echo "  - include/uvbus_v2.h"
echo "  - include/uvrpc_allocator.h"
echo ""
echo "Translation approach:"
echo "1. Use the provided Doxygen examples in DOXYGEN_EXAMPLES.md"
echo "2. Follow the coding standards in CODING_STANDARDS.md"
echo "3. Ensure all comments are in English"
echo "4. Use proper Doxygen format (@brief, @param, @return, etc.)"
echo ""
echo "Example translation:"
echo "  Before: /* 错误码 */"
echo "  After:  /* Error codes */"
echo ""
echo "  Before: /* 传输类型 */"
echo "  After:  /* Transport types */"
echo ""
echo "  Before: // 创建服务器"
echo "  After: // Create server"
echo ""
echo "To generate documentation after translation:"
echo "  ./scripts/generate_docs.sh"
echo ""

# Check if files have been translated
echo "=== Checking Translation Status ==="
for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        COUNT=$(grep -c "[\u4e00-\u9fff]" "$file" || echo "0")
        if [ "$COUNT" -gt 0 ]; then
            echo "$file: ⚠ $COUNT Chinese comments (needs translation)"
        else
            echo "$file: ✓ No Chinese comments"
        fi
    fi
done
echo ""

echo "=== Summary ==="
echo "Files checked: ${#FILES[@]}"
echo "Documentation guide: CODING_STARDS.md"
echo "Doxygen examples: DOXYGEN_EXAMPLES.md"
echo "Doxygen config: Doxyfile"
echo "Documentation generator: scripts/generate_docs.sh"
echo ""
echo "Next steps:"
echo "1. Review CODING_STANDARDS.md for guidelines"
echo "2. Review DOXYGEN_EXAMPLES.md for examples"
echo "3. Translate Chinese comments in the files above"
echo "4. Run ./scripts/generate_docs.sh to generate docs"
echo ""
echo "=== Translation Tool Complete ==="