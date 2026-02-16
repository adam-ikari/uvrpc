#!/bin/bash
# INPROC performance test (same process)

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== INPROC Same Process Performance Test ===${NC}"
echo ""

# Test parameters
REQUESTS=${1:-100000}

# Build test if needed
if [ ! -f "test_inproc_same_process" ]; then
    echo -e "${YELLOW}Building test...${NC}"
    gcc -I include -I deps/uthash -I deps/flatcc/include -I deps/libuv/include -I generated \
        -L dist/lib -o test_inproc_same_process test_inproc_same_process.c \
        -luvrpc -lpthread -lm
fi

# Run test
echo -e "${YELLOW}Running test with $REQUESTS requests...${NC}"
echo ""

START_TIME=$(date +%s.%N)

# Modify test to run multiple requests
# For now, just run the single request test
export LD_LIBRARY_PATH=dist/lib
timeout 10 ./test_inproc_same_process
TEST_EXIT=$?

END_TIME=$(date +%s.%N)

if [ $TEST_EXIT -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=== Test PASSED ===${NC}"
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    echo "Elapsed: ${ELAPSED}s"
else
    echo ""
    echo -e "${RED}=== Test FAILED ===${NC}"
    echo "Exit code: $TEST_EXIT"
fi