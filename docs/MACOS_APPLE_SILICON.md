# Sonus Live Stage — compilación y distribución macOS (M1 / arm64)

**Origen:** adaptación del paquete `Live_stage_mod(3).zip`. Versión CMake: `1.1.4`.
**Estado:** código y herramientas preparados; se requiere una compilación real en macOS.

## 1. Resultados previstos

| Archivo | Destino tras instalar `.pkg` |
|---|---|
| `Sonus Live Stage.app` | `/Applications/Sonus Live Stage.app` |
| `Sonus Live Stage.vst3` | `/Library/Audio/Plug-Ins/VST3/Sonus Live Stage.vst3` |
| `Sonus Live Stage.component` (si se genera) | `/Library/Audio/Plug-Ins/Components/Sonus Live Stage.component` |
| `SonusLiveStage-1.1.4-macOS-arm64.pkg` | `dist/macos-arm64/` para distribución |

**Nota:** Logic Pro utiliza el formato AU `.component`, no VST3. REAPER, Cubase, Studio One y otros DAW compatibles cargan VST3 según su soporte.

## 2. Requisitos en el Mac de compilación

- MacBook Pro M1 (u otro Mac Apple Silicon) con una versión reciente de macOS capaz de ejecutar Xcode y sus Command Line Tools.
- Xcode completo instalado y seleccionado (`sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`).
- CMake 3.22+, Ninja, Git, Node.js/npm (ejemplo: `brew install cmake ninja git node`).
- Internet en la primera compilación para descargar RtAudio, RtMidi, nlohmann/json, JUCE y webview.
- Las muestras y ROMs **no están incluidas**; se cargan desde las ubicaciones del usuario.

El destino de compilación es **macOS 11 o superior en arm64**. No es necesario disponer de macOS 11 en el equipo que compila; se puede compilar en un macOS más reciente con Xcode compatible.

## 3. Compilar y generar el instalador

Desde la raíz del proyecto:

```bash
chmod +x scripts/build_macos.sh scripts/build-pkg.sh
./scripts/build_macos.sh
```

Qué hace el script:

1. Compila React/TypeScript a `ui/app` mediante `npm ci` cuando corresponde y `npm run build`.
2. Configura CMake/Ninja con arquitectura `arm64`, mínimo macOS 11, `STAGE_BUILD_PLUGIN=ON` y ventana nativa.
3. Compila `.app` y VST3 (además del AU si se genera).
4. Prepara la estructura de sistema de archivos del instalador y copia las bibliotecas de fábrica, React y configuración dentro de cada bundle.
5. Genera el `.pkg` mediante `pkgbuild`.

Para no recompilar React: `./scripts/build_macos.sh --skip-ui`.
Para inspeccionar los bundles sin crear el instalador: `./scripts/build_macos.sh --skip-pkg`.

> El `.pkg` sin Developer ID es apropiado **solo para pruebas locales**. Gatekeeper puede bloquearlo al descargarlo en otros Mac. No se debe etiquetar como instalador público firmado.

## 4. Firma y notarización para distribuir

Se necesita una cuenta de Apple Developer con certificados **Developer ID Application** y **Developer ID Installer** en el llavero del Mac. Los nombres deben coincidir con los que devuelve `security find-identity -v -p codesigning`.

Registra previamente un perfil de notarización con `xcrun notarytool store-credentials` y un nombre propio, por ejemplo `live-stage-notary`. No guardes credenciales en el repositorio.

```bash
export STAGE_DEVELOPER_ID_APP='Developer ID Application: TU NOMBRE (TEAMID)'
export STAGE_DEVELOPER_ID_INSTALLER='Developer ID Installer: TU NOMBRE (TEAMID)'
export STAGE_NOTARY_PROFILE='live-stage-notary'
./scripts/build_macos.sh
```

El script firma los bundles, firma el `.pkg`, envía el paquete a Apple Notary Service y aplica `staple` si Apple lo aprueba. Si `notarytool` rechaza la compilación, inspecciona el log, corrige el motivo y reconstruye; no omitas la prueba.

## 5. Validación en una MacBook Pro M1

### Instalación y arquitectura

```bash
pkgutil --check-signature dist/macos-arm64/*.pkg
file 'dist/macos-arm64/payload/Applications/Sonus Live Stage.app/Contents/MacOS/Sonus Live Stage'
# Debe informar Mach-O arm64
sudo installer -pkg dist/macos-arm64/*.pkg -target /
open '/Applications/Sonus Live Stage.app'
```

### Audio / MIDI / UI

- Verifica que la app abre **su propia ventana** y que la página **PERFORM** muestra las ocho memorias (2×4).
- Selecciona salida CoreAudio en SETTINGS; prueba a 44,1/48/96 kHz con buffers de 128/256 muestras.
- Conecta un teclado USB MIDI y prueba pedal sustain, cambios de programa y MIDI ROUTING.
- Carga muestras y prueba Piano (5 capas), Synth (4), Organ, efectos, FX/MIX, ZONES, LOAD/SAVE y recuperación de estado.
- Revisa la carga de CPU, los XRUN y el log en `~/Library/Application Support/Live Stage/logs`.
- Cierra y vuelve a iniciar la app para confirmar que conserva la configuración.

### VST3 y DAW

- Reinicia o reescanea el DAW. Abre `Sonus Live Stage.vst3` como instrumento.
- Comprueba que el editor aparece mediante **WKWebView** y no solicita WebView2.
- Comprueba las notas, sustain, todos los módulos, 8 programas, automatización y guardado/reapertura del proyecto del DAW.
- Si deseas validar AU: `auval -a` para conocer el ID registrado y después `auval -v aumu <subtype> <manufacturer>`; no adivines los códigos que el binario compilado registra.
- Ejecuta `pluginval` (si está disponible) contra el bundle VST3 y prueba varias instancias a la vez.

## 6. Compatibilidad y limitaciones

- Esta adaptación conserva `src/modules/`, `src/core/WorkstationHost.cpp` y los IDs de parámetros del plugin. El procesamiento DSP y el flujo de programas no se han reescrito.
- En la ventana macOS, el tamaño inicial está implementado; la restauración exacta de geometría y la confirmación personalizada al cerrar siguen pendientes. El botón de cierre Cocoa funciona de forma normal.
- Las opciones específicas de Windows (ASIO/WASAPI, WebView2) continúan solamente en Windows. El macOS standalone usa CoreAudio/CoreMIDI y WKWebView; JUCE usa WebKit en el VST3/AU.
- La compilación real en Mac, validación de DAW y firma/notarización **no pueden verificarse desde un entorno Linux**.
- Antes de redistribuir, revisa `docs/THIRD_PARTY_MACOS.md`, en especial **Munt enlazado estáticamente bajo LGPL** y las condiciones de JUCE. No incluyas ROMs ajenas ni bancos que no tengas derecho a redistribuir.

## 7. Construcción remota opcional

Se incluye `.github/workflows/macos-arm64.yml` para ejecutar una compilación **sin firma y no apta para distribución general** en un runner macOS de GitHub Actions. Al publicarlo en tu repositorio, abre Actions y ejecuta el workflow manualmente. No subas certificados privados al repositorio.
