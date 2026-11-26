param([switch]$WithPlugin, [switch]$Demo)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$root=(Get-Location).Path
$pluginFlag=if($WithPlugin){'ON'}else{'OFF'}
$demoFlag=if($Demo){'ON'}else{'OFF'}
$build=if($Demo){'build/win64-demo'}else{'build/win64'}   # CMake caches STAGE_DEMO
cmake -S . -B $build -A x64 "-DSTAGE_BUILD_PLUGIN=$pluginFlag" -DSTAGE_ENABLE_ASIO=ON "-DSTAGE_DEMO=$demoFlag"   # quoted for PS 5.1
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
cmake --build $build --config Release --parallel
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
Push-Location $build
cpack -C Release -G NSIS
if($LASTEXITCODE -ne 0){throw 'NSIS installer generation failed; install NSIS'}
Pop-Location
Write-Host "Installer is in $build. VST3 target is in $build/*_artefacts/Release/VST3."
