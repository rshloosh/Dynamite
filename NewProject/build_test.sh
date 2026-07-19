#!/bin/bash
# Build and run the Dyna-Mite DSP transfer-curve unit tests via the CMake
# console-app target (Dynamite_Tests). Run from the repo root or NewProject/.
set -e

# Resolve repo root (this script lives in NewProject/).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "Configuring (if needed)..."
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "Building Dynamite_Tests..."
cmake --build build --config Release --target Dynamite_Tests -j"$(sysctl -n hw.ncpu)"

# Locate the built test executable (layout varies by generator).
BIN="$(find build -type f -name 'Dynamite_Tests' -perm -u+x 2>/dev/null | head -n1)"
if [ -z "$BIN" ]; then
  BIN="$(find build -path '*Dynamite_Tests*' -type f -perm -u+x 2>/dev/null | grep -v '\.o$' | head -n1)"
fi

if [ -z "$BIN" ]; then
  echo "Error: could not locate the Dynamite_Tests executable under build/."
  exit 1
fi

echo "Running: $BIN"
echo
"$BIN"
