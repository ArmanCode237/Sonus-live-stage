<#
  Sonus Live Stage · VST3 instrument plug-in (JUCE 8) with MSVC (Visual Studio 2022 / Build Tools).
  JUCE does not support MinGW: this script always uses the Visual Studio generator.

  Result:
    dist\VST3\Sonus Live Stage.vst3                      (bundle folder to copy into the DAW's VST3 path)

  Usage (from the project folder):
    .\build-vst3.cmd                 build the VST3 into dist\VST3
    .\build-vst3.cmd -Install        also copy it to C:\Program Files\Common Files\VST3 (run as administrator)
    .\build-vst3.cmd -Clean          rebuild from scratch
    .\build-vst3.cmd -Demo           DEMO edition (src/core/Edition.h) in build\vst3-demo, dist\demo\VST3

  One-time requirement (free): Visual Studio 2022 Build Tools with the C++ workload:
    winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
#>
param([switch]$Install, [switch]$Clean, [switch]$Demo)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

function Step($text) { Write-Host "`n[Sonus Live Stage] $text" -ForegroundColor Cyan }

# MSVC must be installed (vswhere ships with the Visual Studio installer).
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) {
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) {
    throw @"
No se encontró MSVC (Visual Studio 2022 con 'Desarrollo para el escritorio con C++').
Instálalo una vez (gratuito) y vuelve a ejecutar este script:
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
"@
}
Write-Host "  MSVC: $vs"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cmakeVs = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
    if (Test-Path $cmakeVs) { $env:PATH = "$cmakeVs;$env:PATH" } else { throw 'No se encontró CMake 3.22+.' }
}

# Demo and full editions never share a build folder: CMake caches STAGE_DEMO.
$demoFlag = if ($Demo) { 'ON' } else { 'OFF' }
$build = Join-Path $root $(if ($Demo) { 'build\vst3-demo' } else { 'build\vst3' })
if ($Clean -and (Test-Path $build)) { Step "Borrando $build"; Remove-Item -Recurse -Force $build }

# v33: the editor embeds WebView2, linked statically (WebView2LoaderStatic.lib from the
# official Microsoft.Web.WebView2 NuGet package; same version as the vendored headers).
$wv2Lib = Join-Path $root 'third_party\webview2\build\native\x64\WebView2LoaderStatic.lib'
if (-not (Test-Path $wv2Lib)) {
    Step 'Descargando WebView2LoaderStatic.lib (Microsoft.Web.WebView2 1.0.4314.35, nuget.org)'
    $pkg = Join-Path $env:TEMP 'livestage-webview2.nupkg'
    Invoke-WebRequest -UseBasicParsing 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4314.35' -OutFile $pkg
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($pkg)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq 'build/native/x64/WebView2LoaderStatic.lib' } | Select-Object -First 1
        if (-not $entry) { throw 'El paquete de WebView2 no contiene WebView2LoaderStatic.lib' }
        New-Item -ItemType Directory -Force (Split-Path $wv2Lib) | Out-Null
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $wv2Lib, $true)
    } finally { $zip.Dispose(); Remove-Item $pkg -ErrorAction SilentlyContinue }
}
if (-not (Test-Path (Join-Path $root 'ui\app\index.html'))) {
    Write-Host '  Aviso: ui\app no está compilada; ejecuta antes .\build-exe.cmd (o npm run build en webui).' -ForegroundColor Yellow
}

Step 'Configurando (Visual Studio 17 2022, x64, JUCE 8 se descarga la primera vez)'
cmake -S . -B $build -G 'Visual Studio 17 2022' -A x64 -DSTAGE_BUILD_PLUGIN=ON -DSTAGE_BUILD_AUDIO_PROBE=OFF -DSTAGE_ENABLE_ASIO=OFF "-DSTAGE_DEMO=$demoFlag"   # quoted: PS 5.1 passes an unquoted -X=$var literally
if ($LASTEXITCODE) { throw 'La configuración de CMake falló' }

Step 'Compilando el plug-in VST3 (Release)'
cmake --build $build --config Release --target LiveStagePlugin_VST3 --parallel
if ($LASTEXITCODE) { throw 'La compilación del VST3 falló' }

$bundle = Get-ChildItem $build -Recurse -Directory -Filter '*.vst3' |
          Where-Object { $_.FullName -match '\\Release\\VST3\\' } |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1   # the one just built (renames leave old bundles)
if (-not $bundle) { throw 'No se encontró el bundle .vst3 generado' }

$dest = Join-Path $root $(if ($Demo) { 'dist\demo\VST3' } else { 'dist\VST3' })
Step "Copiando a $dest"
New-Item -ItemType Directory -Force $dest | Out-Null
$target = Join-Path $dest $bundle.Name
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse $bundle.FullName $target
# v33: the console (ui) and the factory presets travel inside the bundle (Contents\Resources).
$res = Join-Path $target 'Contents\Resources'
New-Item -ItemType Directory -Force $res | Out-Null
Copy-Item -Recurse (Join-Path $root 'ui') (Join-Path $res 'ui')
foreach ($sub in 'synth\factory', 'synth\dx7\factory', 'organ\factory') {
    $from = Join-Path $root "presets\$sub"
    if (Test-Path $from) {
        $to = Join-Path $res "presets\$sub"
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item -Recurse $from $to
    }
}
# License (AGPL-3.0), third-party notices and the user guide travel with the plug-in.
# ACERCA DE links (support / website / source), read by the engine from Resources\config.
New-Item -ItemType Directory -Force (Join-Path $res 'config') | Out-Null
Copy-Item (Join-Path $root 'config\links.json') (Join-Path $res 'config\links.json')
foreach ($doc in 'LICENSE', 'NOTICE.md', 'docs\GUIA-DE-USO.md') {
    $from = Join-Path $root $doc
    if (Test-Path $from) { Copy-Item $from (Join-Path $res (Split-Path $doc -Leaf)) }
}
Write-Host "  VST3: $target (con la consola y los presets de fábrica en Contents\Resources)"

if ($Install) {
    $system = Join-Path $env:CommonProgramFiles 'VST3'
    Step "Instalando en $system"
    try {
        New-Item -ItemType Directory -Force $system | Out-Null
        $installed = Join-Path $system $bundle.Name
        if (Test-Path $installed) { Remove-Item -Recurse -Force $installed }
        Copy-Item -Recurse $target $installed
        Write-Host "  Instalado: $installed (vuelve a escanear plug-ins en tu DAW)" -ForegroundColor Green
    } catch {
        throw "No se pudo copiar a $system. Ejecuta la consola como administrador o copia a mano '$target'."
    }
}
Step 'Listo'
