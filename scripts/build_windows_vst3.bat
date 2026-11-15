@echo off
setlocal
cd /d "%~dp0.."
cmake -S . -B build-vst -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAGE_BUILD_PLUGIN=ON
if errorlevel 1 exit /b 1
cmake --build build-vst --target LiveStagePlugin_VST3 --parallel 4
exit /b %ERRORLEVEL%
