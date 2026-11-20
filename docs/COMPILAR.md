# Compilar Live Stage (Windows)

Hay dos productos y cada uno tiene su propio comando:

| Producto | Comando | Compilador | Resultado |
|---|---|---|---|
| **App standalone** (`SonusLiveStage.exe`) | `.\build-exe.cmd` | MinGW-w64 (GCC) | `build\release\SonusLiveStage.exe` y `dist\SonusLiveStage-<versión>-win64.zip` (+ instalador `.exe` si tienes NSIS) |
| **Plug-in VST3** | `.\build-vst3.cmd` | MSVC (Visual Studio 2022) | `dist\VST3\Sonus Live Stage.vst3` |

Los dos se ejecutan desde la carpeta del proyecto, en PowerShell o en `cmd`.

---

## 1. App standalone (.exe)

### Requisitos (una sola vez)
- **MinGW-w64** de 64 bits con GCC 13 o superior (UCRT, hilos POSIX), instalado en `C:\mingw64`. El paquete WinLibs ya incluye **CMake** y **Ninja**.
- **Node.js 20+**, solo si quieres recompilar la interfaz React. Con `-SkipUi` no hace falta.
- Opcional: **NSIS** (<https://nsis.sourceforge.io>), para generar además el instalador `.exe`. Sin NSIS se genera solo el ZIP portable.

### Comandos
```powershell
.\build-exe.cmd              # interfaz React + exe + paquete (ZIP / instalador)
.\build-exe.cmd -SkipUi      # conserva la interfaz ya compilada en ui\app
.\build-exe.cmd -NoPackage   # solo el exe, sin empaquetar
.\build-exe.cmd -Clean       # desde cero (vuelve a descargar RtAudio, RtMidi y json)
```

### Qué hace
1. Compila la interfaz (`webui` → `ui\app`), salvo con `-SkipUi`.
2. Configura CMake en `build\release` (Release, ASIO + WASAPI) y compila con Ninja.
3. Comprueba que el `.exe` **no depende de DLL de MinGW**: el runtime de C++ va enlazado de forma estática, así que el `.exe` arranca en cualquier Windows 10/11.
4. Empaqueta con CPack en `dist\`. El paquete lleva el `.exe`, `ui\app`, `presets\`, `programs.json`, `hardware_config.json`, `roms\mt32\LEEME.txt`, la documentación y `LiveStageAudioProbe.exe` (diagnóstico de drivers).

### Distribuir
- **ZIP portable:** se descomprime y se ejecuta `SonusLiveStage.exe`. Los datos del usuario (programas, presets, mapeos, logs, autoguardado) se guardan en `%LOCALAPPDATA%\Live Stage`, nunca junto al ejecutable.
- **Instalador** (con NSIS): instala en `Program Files\Live Stage` y crea el acceso directo «Live Stage» en el menú Inicio.
- **Carpeta de samples:** el paquete no incluye tus samples. En una instalación nueva, registra tu carpeta **una vez** en **LIBRARY → SAMPLE SOURCES → CARPETA → AÑADIR** (queda guardada en tu perfil). También puedes pasarla como argumento en el acceso directo: `SonusLiveStage.exe "D:\Samples"`. Los programas guardados cargan igualmente sus samples, porque guardan la ruta absoluta de cada uno.
- **ASIO:** la compilación incluye ASIO a través de RtAudio. Revisa la licencia del SDK ASIO de Steinberg antes de distribuir el binario públicamente.

### Cómo se abre la app (v32.4)
Al ejecutar `SonusLiveStage.exe` se abre la **ventana «Live Stage»**, sin navegador. Usa Microsoft Edge WebView2, que ya viene con Windows 10/11; si faltara, la app abre la interfaz en el navegador.
- Cerrar la ventana pide confirmación, porque en directo un clic accidental no debe detener el audio.
- **F11** pone la ventana en pantalla completa, y la app recuerda tamaño, posición y pantalla completa.

| Opción de arranque | Qué hace |
|---|---|
| *(ninguna)* | Ventana nativa. Los navegadores no tienen acceso, salvo que actives ACCESO WEB |
| `--browser` | Abre la interfaz en el navegador, como antes (permite el acceso web en esa sesión) |
| `--headless` | Solo motor y servidor, sin ventana: para tests (`npm run smoke…`) y control remoto |
| `--web` | Permite navegadores en esa sesión, aunque ACCESO WEB esté apagado |
| `--lan` | Escucha también en la red local en esa sesión |
| `--console` | Abre una consola con el log en vivo (el log siempre se guarda en `%LOCALAPPDATA%\Live Stage\logs`) |
| `"D:\Samples"` | Carpeta de samples por defecto |

### Seguridad: servidor HTTP
En **SETTINGS → SYSTEM → ACCESO WEB**:
- **NAVEGADOR (apagado por defecto):** solo la ventana de Live Stage puede usar el motor. Se identifica con un token aleatorio nuevo en cada arranque. Cualquier navegador u otro programa recibe un error 403.
- **RED LOCAL:** escucha también en la red, para tablets u otro PC. Se aplica al reiniciar y necesita NAVEGADOR activo para que esos dispositivos entren.

### OSC por IP (estilo REAPER)
En **SETTINGS → MIDI · OSC MAPPINGS → OSC · CONEXIÓN POR IP**:
- **ESTE EQUIPO:** la IP y el puerto de escucha que debes escribir en el controlador (TouchOSC, Open Stage Control, REAPER…).
- **DISPOSITIVO:** la IP y el puerto del controlador. **FEEDBACK** le envía el valor de cada parámetro mapeado por OSC cuando cambia; **SOLO ESTA IP** ignora el OSC de cualquier otra dirección; **PRUEBA** envía `/livestage/ping`.
- Se aceptan mensajes sueltos y *bundles*, con argumentos float, int, double o true/false.
- Si no llega nada desde otro equipo, permite Live Stage en el **Firewall de Windows** (UDP en el puerto de escucha).

---

## 2. Plug-in VST3

### Requisitos (una sola vez)
JUCE 8 **no compila con MinGW**: en Windows necesita MSVC. Instala Visual Studio 2022 Build Tools (gratuito) con la carga de trabajo de C++:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

O bien, desde el instalador de Visual Studio: «Desarrollo para el escritorio con C++».

### Comandos
```powershell
.\build-vst3.cmd             # compila y deja el bundle en dist\VST3\Sonus Live Stage.vst3
.\build-vst3.cmd -Install    # además lo copia a C:\Program Files\Common Files\VST3 (como administrador)
.\build-vst3.cmd -Clean      # desde cero
```

La primera vez descarga JUCE 8 (clon superficial, unos minutos). Después de instalarlo, vuelve a escanear los plug-ins en tu DAW (Reaper, Cubase, Ableton Live, Studio One…).

### Alcance del plug-in (v33)
El VST3 aloja **el mismo núcleo que la app** (`StageApp`): Programs, presets, biblioteca de muestras, browser, mapeos MIDI/OSC y enrutado MIDI.
- **Editor:** la misma consola dentro del DAW (WebView2), servida por un servidor local privado de cada instancia (`127.0.0.1`, puerto libre, token de sesión). Varias instancias funcionan a la vez.
- **Proyecto del DAW:** guarda el setup completo (capas de piano con sus muestras, synth, órgano, FX, mapeos y nombres de preset). Los 8 Programs aparecen como programas del DAW.
- **Automatización:** 27 parámetros con IDs fijos (niveles de módulo y de capa, macros del synth, drawbars, drive y rotary del órgano, envíos de reverb y delay).
- **Audio y MIDI** los gestiona el DAW. El enrutado MIDI por canal permite usar una instancia como multitímbrico (canal 1 → piano, canal 2 → synth…).
- **Sin OSC** en el plug-in (el puerto 9000 lo usa la app; varias instancias chocarían).
- **Requisitos:** Microsoft Edge WebView2 Runtime (incluido en Windows 10/11). Sin él, el editor ofrece abrir la consola en el navegador.
- **ROMs del MT-32:** el plug-in las busca en `%LOCALAPPDATA%\Live Stage\roms\mt32` (no en la carpeta del proyecto).

`build-vst3.cmd` descarga una vez `WebView2LoaderStatic.lib` (paquete oficial Microsoft.Web.WebView2 de nuget.org, 11 MB, fuera de git) y copia la consola (`ui`) y los presets de fábrica dentro del bundle (`Contents\Resources`).

**Probar sin DAW:** `cmake -S . -B build\vst3 -DSTAGE_BUILD_PLUGIN_CHECK=ON` y compila el target `LiveStagePluginHostCheck`; después
`build\vst3\LiveStagePluginHostCheck_artefacts\Release\LiveStagePluginHostCheck.exe "dist\VST3\Sonus Live Stage.vst3" [--editor 30]`
(carga el .vst3 como un DAW: sonido, programas, estado, automatización, dos instancias y el editor).

---

## Enlaces de ACERCA DE (botón de apoyo)
Al pulsar el logo se abre **ACERCA DE**. Sus enlaces se configuran en [`config/links.json`](../config/links.json):

```json
{ "supportUrl": "https://tu-pagina-de-apoyo", "websiteUrl": "", "sourceUrl": "" }
```

- Un campo vacío desactiva (apoyo) u oculta (sitio web, código fuente) su botón.
- El motor abre el enlace en el **navegador predeterminado del sistema**, también desde la ventana de la app o desde el plug-in en el DAW. Solo puede abrir las URLs de este archivo, y solo `http(s)`.
- `build-exe.cmd` y `build-vst3.cmd` lo copian al paquete (`config\links.json` junto al exe, `Contents\Resources\config` en el plug-in). En una instalación ya hecha puedes editar esa copia, o crear `%LOCALAPPDATA%\Live Stage\links.json`, que tiene prioridad.

---

## 3. Recompilar después de hacer cambios

Ya no hay que instalar nada más: los dos scripts recompilan **solo lo que cambió**, en segundos o pocos minutos.

| Qué cambiaste | Comando | Resultado |
|---|---|---|
| Interfaz (`webui/src`) | `.\build-exe.cmd` | Nueva interfaz + exe + ZIP |
| Motor C++ (`src/`) | `.\build-exe.cmd -SkipUi` y `.\build-vst3.cmd` | exe y VST3 con el motor nuevo |
| Interfaz y motor | `.\build-exe.cmd` y `.\build-vst3.cmd` | Todo |
| Solo probar la interfaz en desarrollo | `cd webui; npm run build` y reabrir la app | `ui\app` actualizado |

- **Versión:** cambia `project(SonusLiveStage VERSION x.y.z)` en `CMakeLists.txt`. El ZIP o instalador toma ese número.
- **`-Clean`:** solo hace falta si cambias versiones de dependencias (RtAudio, JUCE, webview…) o si un build queda en un estado raro.
- **Antes de publicar una versión:** tests del motor, smokes con `--headless` y `npm run soak -- 30` (detalle en la sección 5).
- **Validar el VST3** con [pluginval](https://github.com/Tracktion/pluginval) (gratuito):
  `pluginval --strictness-level 5 --validate "dist\VST3\Sonus Live Stage.vst3"`.
  Después, prueba el plug-in en tu DAW.

## 4. macOS Apple Silicon (arm64, .app + VST3 + .pkg)

La adaptación multiplataforma está incluida. Consulta [`MACOS_APPLE_SILICON.md`](MACOS_APPLE_SILICON.md).
En un **Mac con Apple Silicon, Xcode, CMake, Ninja, Git y Node.js**:

```bash
./scripts/build_macos.sh
# Resultado: dist/macos-arm64/SonusLiveStage-1.1.4-macOS-arm64.pkg
```

La app usa **WKWebView** en una ventana propia; el VST3 usa el editor JUCE con WebKit.
El paquete instala la app en `/Applications` y el VST3 en
`/Library/Audio/Plug-Ins/VST3`. Si JUCE genera el AU, también se empaqueta para Logic.
El script admite `--skip-ui` si la React UI ya está compilada.

**No son binarios verificados hasta compilarse en macOS y probarse en hardware M1.**
El script puede firmar con Developer ID y notarizar opcionalmente mediante variables de entorno;
para distribución pública, la firma/notarización y la auditoría de licencias son esenciales.

## 5. Desarrollo y tests

```powershell
# compilación de desarrollo (la que usa el día a día)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\SonusLiveStage.exe

# tests del motor (26), en una carpeta aparte
cmake -S . -B build-tests -G Ninja -DSTAGE_BUILD_SAMPLE_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests

# interfaz: tests unitarios, smokes y prueba de resistencia
# (los smokes necesitan la app en modo --headless o con ACCESO WEB activado)
.\build\SonusLiveStage.exe --headless
cd webui
npm test
npm run smoke
npm run soak -- 10        # 10 minutos tocando y cambiando de programa
```

> En compilaciones de desarrollo con MinGW, `build\SonusLiveStage.exe` también va enlazado de forma estática y arranca sin `C:\mingw64\bin` en el PATH.

## Scripts anteriores
`scripts\build_windows.bat`, `build_windows.ps1`, `build_windows_mingw.ps1` y `build_windows_vst3.bat`, así como la carpeta `plugin\` (con otra definición de plug-in), son de versiones anteriores. Quedan sustituidos por `build-exe.cmd` y `build-vst3.cmd`, que usan `cmake\StagePlugin.cmake` y `src\plugin\`.
