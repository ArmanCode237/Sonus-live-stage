@echo off
rem Live Stage: compila el plug-in VST3 (MSVC / Visual Studio 2022) en dist\VST3
rem Opciones: -Install (copia a C:\Program Files\Common Files\VST3, como administrador)  -Clean  -Demo
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-vst3.ps1" %*
exit /b %ERRORLEVEL%
