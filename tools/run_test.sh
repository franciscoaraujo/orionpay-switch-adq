#!/bin/bash
# run_test.sh - Run project tests in WSL

set -e

echo "Running tests in WSL..."
# Example: Run CMake and ctest (adjust as needed for your project)
mkdir -p build
cd build
cmake ..
make
ctest --output-on-failure
