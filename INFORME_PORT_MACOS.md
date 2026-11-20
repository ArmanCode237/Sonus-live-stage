# Adaptación de Sonus Live Stage a macOS (Apple Silicon)

**Origen:** `Live_stage_mod(3).zip` (versión indicada por `CMakeLists.txt`: 1.1.4).
**Destino:** MacBook Pro M1, arquitectura arm64, macOS 11 o posterior.

## Entrega real

Se entrega **proyecto fuente modificado**, scripts de compilación/instalación, workflow de GitHub Actions y pruebas estáticas. **NO se entrega todavía un binario macOS (.app, .vst3 o .pkg) compilado**, pues este entorno no tiene Xcode/macOS y no sería fiable fabricar uno con herramientas Linux.

En una Mac Apple Silicon, `./scripts/build_macos.sh` prepara:

- `dist/macos-arm64/payload/Applications/Sonus Live Stage.app`
- `dist/macos-arm64/payload/Library/Audio/Plug-Ins/VST3/Sonus Live Stage.vst3`
- `dist/macos-arm64/SonusLiveStage-1.1.4-macOS-arm64.pkg` (instalador combinado)
- AU `.component` opcional si JUCE lo genera, apropiado para Logic Pro.

## Cambios efectuados

1. CMake: construcción nativa macOS `.app`, `Info.plist`, arquitectura arm64 y preparación de recursos dentro de `Contents/Resources`.
2. Standalone: WebView2 en Windows intacto; nuevo frontend nativo WKWebView en macOS.
3. Plug-in: JUCE usa WebKit en macOS sin solicitar WebView2; recuperación de los assets desde el propio binario del VST3, no desde el DAW.
4. Persistencia: los archivos iniciales se copian de los recursos de la app al directorio de usuario en el primer arranque; sin escribir sobre el bundle instalado.
5. Audio: se activa la ruta CoreAudio/CoreMIDI y se habilita flush-to-zero en el callback de ARM64.
6. Instalador: empaquetado de `.app`, VST3 y AU opcional; firma Developer ID y notarización disponibles solo cuando se proporcionan los certificados locales.
7. CI: workflow `.github/workflows/macos-arm64.yml` para generar un PKG arm64 sin firma (solo pruebas, no distribución pública).
8. Documentación: `docs/MACOS_APPLE_SILICON.md` y `docs/THIRD_PARTY_MACOS.md`.

Los motores de piano, sintetizador y órgano y `WorkstationHost` no se han modificado. El plug-in mantiene sus identificadores y parámetros de automatización.

## Comprobaciones realizadas en este entorno

- `bash -n scripts/build_macos.sh scripts/build-pkg.sh`: **correcto**.
- XML de `Info.plist.in` analizado como plist: **correcto**.
- `c++ -std=c++17 -Isrc -Ithird_party -fsyntax-only src/app/UserDataPaths.cpp src/app/NativeWindow.cpp` en Linux: **correcto** (no compila las ramas macOS).
- `python3 tests/macos_port_contract.py`: **17 comprobaciones estáticas superadas**.
- Ejecución Linux de `./scripts/build_macos.sh --skip-ui`: **se detiene de forma intencional y explícita**, solicitando macOS/Xcode.

## Pendiente imprescindible para declarar la versión disponible

1. Compilar en MacBook Pro M1 o un runner macOS ARM64 con Xcode.
2. Revisar errores de compilación/link que solo aparezcan en Apple Clang y corregirlos si existen.
3. Probar la app, el audio CoreAudio, MIDI CoreMIDI, React/WKWebView y todas las funciones en ese equipo.
4. Validar VST3 con `pluginval`, un DAW real y varias instancias, guardado/reapertura.
5. Para distribuir a otros usuarios: firma de `.app` y VST3, firma de `.pkg`, notarización y evaluación de licencias (especialmente libmt32emu LGPL con enlace estático).

## Cómo usar la entrega

Descomprime el proyecto completo en un Mac Apple Silicon. Instala Xcode, CMake, Ninja, Git y Node.js, y ejecuta:

```bash
chmod +x scripts/build_macos.sh
./scripts/build_macos.sh
```

Para omitir la recompilación React, ya incluida en `ui/app`: `./scripts/build_macos.sh --skip-ui`.
La guía de pruebas e instrucciones de firma están en `docs/MACOS_APPLE_SILICON.md`.
