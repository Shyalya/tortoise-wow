#!/bin/bash
set -e

# Find repo root relative to the script location
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "${SCRIPT_DIR}/../../.." && pwd )"
BUILD_DIR="${REPO_ROOT}/build"

echo "=========================================================="
echo " Dungeon Clear Test Runner"
echo "=========================================================="
echo "Repository Root: ${REPO_ROOT}"
echo "Build Directory: ${BUILD_DIR}"
echo "----------------------------------------------------------"

# Configure with unit testing enabled
echo "Configuring build system with BUILD_TESTING=ON..."
cd "${REPO_ROOT}"
cmake -B build -DBUILD_TESTING=ON

# Compile the dungeon_clear_tests target specifically
echo "Building dungeon_clear_tests target..."
cmake --build build --target dungeon_clear_tests -j$(nproc)

# Run the DungeonClear test cases. The binary is the module's standalone suite,
# so this covers the DungeonClear* fixtures plus the Dc* suites (the approach
# regressions and the replay harness round-trip/fixture runner).
echo "Running unit tests..."
cd "${BUILD_DIR}"
./dungeon_clear_tests --gtest_filter='*DungeonClear*:Dc*:RoomAggro*:BossRoster*:DungeonEvent*:EventBuilder*'

echo "----------------------------------------------------------"
echo "Tests completed successfully!"
echo "=========================================================="
