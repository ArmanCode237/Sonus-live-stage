@echo off
setlocal
cd /d "%~dp0.."
cmake -S . -B build-v16 -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build-v16 --parallel 4
if errorlevel 1 exit /b 1
cpack --config build-v16\CPackConfig.cmake -G ZIP
if errorlevel 1 exit /b 1
where makensis >nul 2>&1
if errorlevel 1 (
  echo NSIS no instalado: ZIP portable generado. Instala NSIS para generar .exe instalable.
  exit /b 0
)
cpack --config build-v16\CPackConfig.cmake -G NSIS
exit /b %ERRORLEVEL%
