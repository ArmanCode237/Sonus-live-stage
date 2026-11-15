# Standalone executable for users with a MinGW-w64 POSIX compiler.
# Requires the ASIO sources shipped as part of RtAudio 6.0.1.
# Reconfigure in a new build folder to avoid stale RTAUDIO_API_ASIO=OFF caches.
# For JUCE VST3 prefer build_windows.ps1 (MSVC/Visual Studio 2022).
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
cmake -S . -B build/mingw -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DSTAGE_ENABLE_ASIO=ON -DSTAGE_BUILD_PLUGIN=OFF
if($LASTEXITCODE -ne 0){throw 'CMake configuration failed'}
cmake --build build/mingw --parallel
if($LASTEXITCODE -ne 0){throw 'MinGW build failed'}
Push-Location build/mingw
cpack -C Release -G NSIS
if($LASTEXITCODE -ne 0){throw 'NSIS is required to create .exe installer'}
Pop-Location
