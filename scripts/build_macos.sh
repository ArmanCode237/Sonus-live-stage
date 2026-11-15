#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build/macos -DCMAKE_BUILD_TYPE=Release -DSTAGE_BUILD_PLUGIN=ON
cmake --build build/macos --config Release --parallel "$(sysctl -n hw.ncpu)"
(cd build/macos && cpack -G DragNDrop)
echo 'macOS .dmg available in build/macos; VST3/AU in plugin artefacts.'
