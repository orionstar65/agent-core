#!/bin/bash
# Build script for Nucleus IoT Platform
# Builds agent-core and all extensions

set -e

BUILD_TYPE="${1:-Release}"

echo "================================================"
echo "Building Nucleus IoT Platform (${BUILD_TYPE})"
echo "================================================"
echo ""

# Build Agent Core
echo ">>> Building Agent Core..."
cd agent-core
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j$(nproc 2>/dev/null || echo 4)
cd ..
echo "✓ Agent Core built"
echo ""

# Build Tunnel Extension
echo ">>> Building Tunnel Extension..."
cd extensions/tunnel
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j$(nproc 2>/dev/null || echo 4)
cd ../..
echo "✓ Tunnel Extension built"
echo ""

# Build PS-Exec Extension
echo ">>> Building PS-Exec Extension..."
cd extensions/ps-exec
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j$(nproc 2>/dev/null || echo 4)
cd ../..
echo "✓ PS-Exec Extension built"
echo ""

# Build Sample Extension
echo ">>> Building Sample Extension..."
cd extensions/sample
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j$(nproc 2>/dev/null || echo 4)
cd ../..
echo "✓ Sample Extension built"
echo ""

echo "================================================"
echo "Build Complete!"
echo "================================================"
echo ""
echo "Binaries:"
echo "  Agent Core:         agent-core/build/agent-core"
echo "  Tunnel Extension:   extensions/tunnel/build/ext-tunnel"
echo "  PS-Exec Extension:  extensions/ps-exec/build/ext-ps"
echo "  Sample Extension:   extensions/sample/build/sample-ext"
echo ""
echo "To run agent-core:"
echo "  cd agent-core"
echo "  ./build/agent-core --config ./config/dev.json"
