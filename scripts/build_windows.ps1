param([switch]$WithPlugin)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root=(Get-Location).Path
$pluginFlag=if($WithPlugin){'ON'}else{'OFF'}
cmake -S . -B build/win64 -A x64 -DSTAGE_BUILD_PLUGIN=$pluginFlag -DSTAGE_ENABLE_ASIO=ON
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
cmake --build build/win64 --config Release --parallel
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
Push-Location build/win64
cpack -C Release -G NSIS
if($LASTEXITCODE -ne 0){throw 'NSIS installer generation failed; install NSIS'}
Pop-Location
Write-Host "Installer is in build/win64. VST3 target is in build/win64/*_artefacts/Release/VST3."
