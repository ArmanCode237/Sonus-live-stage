<#
  Sonus Live Stage · standalone build (SonusLiveStage.exe) with MinGW-w64 + Ninja.

  Result:
    build\release\SonusLiveStage.exe          (runs from the project folder)
    dist\SonusLiveStage-<version>-win64.zip          (portable: exe + ui + presets)
    dist\SonusLiveStage-<version>-win64.exe          (installer, only if NSIS is installed)

  Usage (from the project folder):
    .\build-exe.cmd                 full build: React UI + exe + package
    .\build-exe.cmd -SkipUi         keep the current ui\app (no Node.js needed)
    .\build-exe.cmd -NoPackage      only the exe
    .\build-exe.cmd -Clean          rebuild from scratch (new dependency download)
#>
param([switch]$SkipUi, [switch]$NoPackage, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

function Step($text) { Write-Host "`n[Sonus Live Stage] $text" -ForegroundColor Cyan }
function Need($tool, $hint) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "No se encontró '$tool'. $hint" }
}

# Toolchain: MinGW-w64 (GCC 13+), CMake 3.22+, Ninja. C:\mingw64\bin is added if present.
$mingw = 'C:\mingw64\bin'
if (Test-Path $mingw) { $env:PATH = "$mingw;$env:PATH" }
Need 'g++'   'Instala MinGW-w64 (por ejemplo WinLibs, UCRT, POSIX threads) en C:\mingw64.'
Need 'cmake' 'Instala CMake 3.22 o superior (incluido en WinLibs).'
Need 'ninja' 'Instala Ninja (incluido en WinLibs).'

$build = Join-Path $root 'build\release'
$dist = Join-Path $root 'dist'
if ($Clean -and (Test-Path $build)) { Step 'Borrando build\release'; Remove-Item -Recurse -Force $build }

if (-not $SkipUi) {
    Need 'npm' 'Instala Node.js 20+ o usa -SkipUi para conservar la interfaz ya compilada en ui\app.'
    Step 'Compilando la interfaz React (webui -> ui\app)'
    Push-Location (Join-Path $root 'webui')
    try {
        if (-not (Test-Path 'node_modules\vite')) { npm ci; if ($LASTEXITCODE) { throw 'npm ci falló' } }
        npm run build; if ($LASTEXITCODE) { throw 'La compilación de la interfaz falló' }
    } finally { Pop-Location }
}

Step 'Configurando (Release, ASIO + WASAPI, runtime estático)'
cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAGE_ENABLE_ASIO=ON -DSTAGE_BUILD_PLUGIN=OFF
if ($LASTEXITCODE) { throw 'La configuración de CMake falló' }

Step 'Compilando SonusLiveStage.exe'
cmake --build $build --parallel
if ($LASTEXITCODE) { throw 'La compilación falló' }

# The exe must not depend on MinGW DLLs (it has to start on any PC).
$exe = Join-Path $build 'SonusLiveStage.exe'
if (Get-Command objdump -ErrorAction SilentlyContinue) {
    $deps = objdump -p $exe | Select-String 'DLL Name: (libstdc\+\+|libgcc|libwinpthread)'
    if ($deps) { throw "El exe todavía depende de DLL de MinGW: $($deps -join ', ')" }
    Write-Host '  Sin dependencias de MinGW: el exe es portable.' -ForegroundColor Green
}

if (-not $NoPackage) {
    Step 'Empaquetando en dist\'
    New-Item -ItemType Directory -Force $dist | Out-Null
    cpack --config (Join-Path $build 'CPackConfig.cmake') -B $dist
    if ($LASTEXITCODE) { throw 'El empaquetado (CPack) falló' }
    Remove-Item -Recurse -Force (Join-Path $dist '_CPack_Packages') -ErrorAction SilentlyContinue
    if (-not (Get-Command makensis -ErrorAction SilentlyContinue) -and -not (Test-Path 'C:\Program Files (x86)\NSIS\makensis.exe')) {
        Write-Host '  NSIS no está instalado: se generó solo el ZIP portable (instala NSIS para el instalador .exe).' -ForegroundColor Yellow
    }
}

Step 'Listo'
Write-Host "  Ejecutable : $exe"
if (-not $NoPackage) { Get-ChildItem $dist -File | ForEach-Object { Write-Host "  Paquete    : $($_.FullName)" } }
