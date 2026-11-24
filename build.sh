#!/usr/bin/env bash
# Build COD Redemption Hunter (C++ / Qt 6) on Linux or macOS.
#   Debian/Ubuntu/Kali: sudo apt install build-essential cmake ninja-build libsqlite3-dev qt6-base-dev qt6-webengine-dev
#   Fedora:             sudo dnf install gcc-c++ cmake ninja-build sqlite-devel qt6-qtbase-devel qt6-qtwebengine-devel
#   Arch:               sudo pacman -S base-devel cmake ninja sqlite qt6-base qt6-webengine
#   macOS:              brew install cmake ninja qt
set -euo pipefail
cd "$(dirname "$0")"
GEN=$(command -v ninja >/dev/null && echo Ninja || echo "Unix Makefiles")
EXTRA=()
if command -v brew >/dev/null && [ -d "$(brew --prefix qt 2>/dev/null)" ]; then EXTRA+=(-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"); fi
if ! cmake -S . -B build -G "$GEN" -DCMAKE_BUILD_TYPE=Release "${EXTRA[@]}"; then
  echo "Qt 6 not found? Install it (see the top of this file) or build only the core: cmake -S . -B build -DHUNTER_BUILD_APP=OFF"
  exit 1
fi
cmake --build build --parallel
ctest --test-dir build --output-on-failure
echo
echo "Built: build/CODRedemptionHunter   (run it, or: build/CODRedemptionHunter --scan-once)"
