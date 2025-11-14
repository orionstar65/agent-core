#!/bin/bash
# Build script for Agent Core

set -e

BUILD_TYPE="${1:-Release}"

echo "Building Agent Core (${BUILD_TYPE})..."
echo ""

# Create build directory
mkdir -p build

# Configure
echo "Configuring..."
cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

# Build
echo "Building..."
cmake --build build -j$(nproc 2>/dev/null || echo 4)

echo ""
echo "Build complete!"
echo "Binary: build/agent-core"
