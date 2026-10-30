@echo off
rem Build every mod in mods\ into out\<name>.so
rem Needs: Android NDK (r27c), CMake, Ninja, Python 3. Set ANDROID_NDK to your NDK folder.
cd /d "%~dp0"
if "%ANDROID_NDK%"=="" (
  echo Set ANDROID_NDK to your Android NDK folder ^(r27c recommended^).
  exit /b 1
)
python tools\check_mod.py || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
echo Done. Your mods are in out\
dir /b out\*.so
