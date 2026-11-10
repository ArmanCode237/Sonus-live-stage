# Live Stage — Piano de 3 capas + EQ Main + Drawbars mecánicos

## Archivos modificados
- `src/core/Nordtypes.h`: parámetros persistentes de piano (módulo, volumen, capa 1–3).
- `src/modules/NordPianoEngine.h/.cpp`: 3 instrumentos de muestras simultáneos, voces seguras, 3 ganancias y mute independientes.
- `src/core/WorkstationHost.cpp`: nuevos parámetros de control en el registro.
- `src/main.cpp`: carga por capa, descarga de capa, estado y persistencia de los bancos de muestra por programa.
- `ui/index.html`, `ui/css/piano.css`, `ui/js/piano.js`: mezclador piano y EQ MAIN expandido.
- `ui/js/{core,sync,knobs,library,polling,programs}.js`: controles, navegador, estado de UI.
- `ui/css/drawbars.css`, `ui/js/organ.js`, `ui/organ.html`: tiradores de órgano mecánicos escalonados 0–8.

## Uso
1. Abre **MAIN** y activa PIANO si está apagado.
2. Selecciona L1, L2 o L3 en SOUND BANK. Abre BROWSER y carga un .npno/.nsmp en esa capa.
3. Enciende/apaga cada capa con su LED y ajusta su volumen con su deslizador; MASTER regula solo el piano.
4. Los ocho programas guardan los tres paths de sample y los parámetros de mezcla. Los presets del sintetizador y del órgano se mantienen separados.
5. La sección Voice / Loops deja de mostrarse en MAIN, pero el modal sigue disponible para compatibilidad con su lógica interna.
6. En ÓRGANO, mueve los nueve drawbars en pasos 0–8 (internamente 0–1 para el DSP); los colores son solo visuales.

## Compatibilidad
- Los programas antiguos, sin `pianoLayers`, siguen cargando su `sourcePath` en L1; L2 y L3 quedan vacías.
- El parámetro `volume` preexistente sigue siendo el volumen general de la workstation; `pianoVolume` es nuevo y exclusivo del piano.
- No sobrescribir un `programs.json` del usuario sin hacer copia de respaldo.
- El cambio de sample mantiene las notas que siguen sonando hasta que finalice su envolvente; la voz conserva el ownership de su banco.

## Compilación en Windows
Para garantizar que Ninja reconozca todos los cambios, **elimina la carpeta `build` antes de compilar**. Desde CMD, en la raíz del proyecto: `rmdir /s /q build`, después `cmake -S . -B build -G Ninja` y `cmake --build build --parallel 4`. También puedes usar el archivo `RECOMPILAR_LIVE_STAGE.cmd` desde esa raíz. Los ZIP usan fechas de modificación locales anteriores a la fecha de entrega para evitar el bucle de regeneración de Ninja que se presentó previamente.

## Pruebas
Comprobado:
- Test de C++ de tres capas con sanitizadores ASan/UBSan: 3 voces concurrentes, mute por capa y módulo, cambio de banco con voces activas, roundtrip de parámetros.
- Test de los 8 sonidos de fábrica del sintetizador a 44.1/48/96 kHz.
- `node --check` de todos los scripts JavaScript.
- `tests/piano_layers_contract.py` y el test previo `tests/check_integration.py`.

Pendiente: compilación integral en el entorno Windows del usuario con RtAudio/RtMidi y pruebas con teclado MIDI real. Las capturas de renderizado visual automatizadas no estuvieron disponibles en este entorno.
