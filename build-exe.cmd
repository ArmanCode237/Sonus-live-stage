@echo off
rem Live Stage: compila SonusLiveStage.exe (MinGW) y lo empaqueta en dist\
rem Opciones: -SkipUi  -NoPackage  -Clean  -Demo
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-exe.ps1" %*
exit /b %ERRORLEVEL%
