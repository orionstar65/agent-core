#!/bin/bash
# Clean script for Nucleus IoT Platform
# Removes all build artifacts

echo "Cleaning all build artifacts..."

rm -rf agent-core/build
rm -rf extensions/tunnel/build
rm -rf extensions/ps-exec/build
rm -rf extensions/sample/build

echo "Clean complete!"
