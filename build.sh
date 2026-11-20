#!/bin/bash
# Compile et installe oraclee (AU + VST3 + appli autonome + CLI)
set -e
cd "$(dirname "$0")"
git submodule update --init --recursive 2>/dev/null || true
if [ ! -f external/JUCE/CMakeLists.txt ]; then
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git external/JUCE
fi
GEN=""; command -v ninja >/dev/null && GEN="-G Ninja"
cmake -S . -B build $GEN -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
echo "OK - oraclee installé dans ~/Library/Audio/Plug-Ins (AU + VST3)"
