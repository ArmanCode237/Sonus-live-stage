@echo off
setlocal
cd /d "%~dp0"
if not exist "CMakeLists.txt" (
    echo ERROR: Este archivo debe estar junto al CMakeLists.txt.
    pause
    exit /b 1
)
echo.
echo Se va a eliminar SOLO la carpeta temporal "build" para regenerar Ninja.
echo Se conservan los presets, programs.json y hardware_config.json.
if exist "build" (
    rmdir /s /q "build"
    if exist "build" (
        echo ERROR: No se pudo eliminar build. Cierra la app y vuelve a intentar.
        pause
        exit /b 1
    )
)
cmake -S . -B build -G Ninja
if errorlevel 1 goto failed
cmake --build build --parallel 4
if errorlevel 1 goto failed
echo.
echo COMPILACION COMPLETADA.
pause
exit /b 0
:failed
echo.
echo COMPILACION FALLIDA. Revisa el primer mensaje de error.
pause
exit /b 1
