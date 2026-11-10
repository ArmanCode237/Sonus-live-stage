# LIVE STAGE V6 — Implementaciones

Base: Live Stage V5, conservando su motor de cinco capas de piano, sintetizador de tres capas, órgano y rack AUX.

## Funciones incorporadas

1. **MAIN / scroll de layers:** el contenedor de las cinco capas tiene una altura máxima limitada, scroll interno y ajustes para pantallas de poca altura. No aumenta indefinidamente la columna ni el panel principal.
2. **Organ FX:** se conserva y facilita el editor propio del órgano (`ORGAN FX · EDIT`), con rotary, scanner vibrato/chorus, drive y percusión. Sus controles actúan sobre parámetros propios del órgano, separados de los envíos AUX generales.
3. **Sintetizador / 48 patches:** los 12 originales y 36 nuevos (6 familias × 6 variaciones), clasificados como Keys, Pads, Brass, Bass, Leads, Saws; filtrado por familia y campo de búsqueda. La selección de un patch modifica solo el sintetizador. Los presets personales del sintetizador se conservan en su banco anterior.
4. **STORE:** abre un diálogo para seleccionar destino 1–8 y nombre de hasta 48 caracteres. El nombre se guarda en `programs.json` y aparece en la pantalla MAIN. Al recuperar un programa también se recupera su nombre.
5. **INIT:** reinicia el buffer de interpretación actual sin escribir sobre los ocho programas existentes. Atención: inicializa los motores y descarga las capas actuales de piano; guarda primero tu programa si deseas conservar cambios. Solo STORE escribe el programa inicializado en un slot.
6. **Categorías de samples:** ampliadas categorías de pianos, teclados eléctricos, percusión afinada, sintetizadores, bajos, orquesta, cinemáticos y texturas. Las etiquetas de los archivos siguen gestionándose mediante `/api/library/tag`.
7. **MIXER / SAMPLE CONFIG:** 5 canales editables por pestañas; cada capa incluye Attack, Decay, Sustain, Release, Loop Hold, Pan, Fine Tune, Cutoff y Resonance, más tres envíos AUX (Mod, Delay, Reverb). Se conservan la matriz AUX global y el volumen/activación por capa en MAIN. El filtrado SVF de cutoff/resonancia se aplica a cada señal de piano antes de combinarla con la matriz AUX. Los parámetros quedan incluidos en los ocho programas.

## Archivos y compatibilidad

- Motor: `src/core/Nordtypes.h`, `src/core/WorkstationHost.cpp`, `src/modules/NordPianoEngine.h/.cpp`, `src/modules/NordSynthModule.cpp`, `src/main.cpp`.
- UI: `ui/index.html`, `ui/synth.html`, `ui/partials/modal-piano-sample.html`, `ui/js/core.js`, `ui/js/programs.js`, `ui/js/piano-sample-editor.js`, `ui/js/synth.js`, `ui/js/polling.js`, `ui/css/piano.css`, `ui/css/synth.css`.
- Se reutiliza el modal existente del órgano y sus efectos dedicados, que ya formaba parte de V5.
- **El ZIP incremental no contiene `programs.json`, `hardware_config.json` ni `sample_tags.json`.** Los programas antiguos sin filtro por capa recuperan cutoff abierto y Q neutra; los presets antiguos del sintetizador mantienen sus IDs.

## Instalación (Windows + MinGW + Ninja)

1. Respaldar todo el proyecto y especialmente `programs.json`, `hardware_config.json`, samples y presets personales.
2. Extraer el ZIP **Actualizados** en la raíz de Live Stage V5, conservando rutas relativas.
3. Ejecutar desde la raíz:

```bat
cmake -S . -B build -G Ninja
cmake --build build --parallel 4
```

4. Reiniciar Live Stage y recargar la UI con Ctrl+F5.

## Verificaciones

- Todas las fuentes `ui/js/*.js` pasan `node --check`.
- Contrato HTML: 60 controles en el mezclador de piano (5 × [9 parámetros de muestra + 3 envíos]), matriz global con 27 perillas, formulario Store/Init y 48 nombres de patch.
- Compilación y ejecución aislada del DSP C++: cinco capas, parámetros de filtro por capa, CC1/pitch bend, 48 presets y efectos de órgano. Resultado OK.
- `tests/check_integration.py`: OK.

**No verificado:** compilación integral en Windows con RtAudio/RtMidi, pruebas con hardware MIDI real y mediciones de rendimiento/latencia. La compilación aislada no las sustituye.
