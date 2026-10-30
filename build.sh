#!/usr/bin/env bash
# Build every mod in mods/ into out/<name>.so
# Needs the Android NDK (r27c recommended): set ANDROID_NDK=/path/to/android-ndk
set -euo pipefail
cd "$(dirname "$0")"
NDK="${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
  echo "Set ANDROID_NDK to your Android NDK folder (r27c recommended)." >&2
  exit 1
fi
python3 tools/check_mod.py
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
cmake --build build
echo "Done. Your mods are in out/"
ls -1 out/*.so
