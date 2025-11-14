#!/bin/bash
# Development run script for Agent Core

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Agent Core - Development Runner${NC}"
echo ""

# Build if needed
if [ ! -f "build/agent-core" ]; then
    echo -e "${YELLOW}Binary not found, building...${NC}"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
fi

# Run
echo -e "${GREEN}Starting Agent Core...${NC}"
echo ""

./build/agent-core --config ./config/dev.json

echo ""
echo -e "${GREEN}Agent Core exited${NC}"
