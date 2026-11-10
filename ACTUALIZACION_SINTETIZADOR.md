# Live Stage — actualización del sintetizador analógico (3 capas)

## Cambios principales

- Conserva exactamente **3 capas independientes** en `NordSynthModule`.
- Añade **JUNO DCO** (tipo de onda `8`): mezcla de sierra, pulso con PWM, suboscilador cuadrado una octava abajo y ruido.
- Añade **JUPITER DUAL** (tipo de onda `9`): bloque analógico anterior más oscilador B con nivel, forma de onda, intervalo y desafinación propios.
- Control de ancho de pulso, velocidad/profundidad de PWM, filtro pasaaltas, saturación suave y mezcla de osciladores.
- **Filtro por voz**: topología SVF estable, modos LP/HP/BP, tracking de teclado, velocidad y envolvente de filtro independiente por nota. Cálculo de coeficientes a 1/16 de la frecuencia de audio para reducir carga de CPU.
- LFO principal editable (seno, triángulo, diente de sierra, cuadrada) con velocidad, intensidad, vibrato y destino de corte de filtro.
- Ocho sonidos de fábrica diferenciados; los sonidos 5–8 ya no son variantes idénticas de «Analog Strings».
- UI HTML/CSS/JS conservando la identidad oscura/roja, los botones y las perillas existentes. Los apartados avanzados son plegables.
- Banco de presets **de sintetizador** y banco de presets **de órgano** accesibles desde sus respectivas páginas. Se almacenan como `synth_presets.json` y `organ_presets.json` (creados cuando se guarda el primer preset).
- Los **8 programas globales** del panel MAIN siguen siendo snapshots del teclado completo: sample de piano, activación/configuración de módulos, 3 capas del synth, efectos/EQ, drawbars y modulación. Ahora también incluyen los cuatro LFO y las tres envolventes de configuración disponibles en C++.
- `programs.json` original se conserva sin modificación. Los valores de campos nuevos tienen defaults para poder abrir programas antiguos.
- Se corrige `NordTypes.h` → `Nordtypes.h` en las inclusiones con capitalización incompatible en Linux/macOS.

**Importante:** Estos diseños son inspiraciones funcionales en la síntesis analógica de la familia Juno/Jupiter; no son modelos exactos de los circuitos Roland originales. Se conserva el chorus global de Live Stage; no se reproduce el circuito BBD original de Juno. El tipo de onda histórico `7` (Sample), cuya ruta de reproducción de muestras no estaba implementada en el motor de sintetizador, no se expone entre las nuevas formas de onda de la interfaz.

## Presets de instrumento vs. programas MAIN

| Acción | Qué almacena / restaura |
|---|---|
| Sintetizador > presets de fábrica 1–8 | Solo las 3 capas del sintetizador; la carga habilita el módulo synth si estaba apagado |
| Sintetizador > GUARDAR SONIDO / CARGAR | 3 capas y valores globales del sintetizador, LFO y envelopes; **no** modifica presets de órgano ni efectos globales |
| Órgano > GUARDAR ÓRGANO / CARGAR | Modelo, drawbars, percusión, rotary y configuración del órgano; **no** modifica synth ni efectos globales |
| MAIN > STORE + botón 1–8 | Un programa completo de interpretación: piano + synth + órgano + FX + EQ, y sus configuraciones actuales |
| MAIN > botón 1–8 (sin STORE) | Recupera ese programa global completo |

## Instalación

1. Haz una copia de seguridad de tu carpeta original y especialmente `programs.json`, `hardware_config.json`, `samples/`, y cualquier `synth_presets.json` / `organ_presets.json` que ya exista.
2. Para integrar en tu proyecto estable, extrae el ZIP **solo-archivos-actualizados** encima de tu carpeta raíz conservando las rutas `src/...` y `ui/...`; **no** borres archivos no incluidos.
3. Alternativamente, usa el ZIP **proyecto-completo**, que incorpora todo lo que traía el archivo original y las modificaciones; recuerda fusionar tus archivos de datos locales antes de usarlo.
4. Desde la raíz del proyecto, recompila con CMake (requiere C++17, RtAudio, RtMidi, nlohmann/json; CMake los obtiene mediante `FetchContent`):

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

5. Inicia el ejecutable desde la carpeta raíz del proyecto y abre `http://localhost:8080/synth`.
6. Activa el synth, carga un preset de fábrica, selecciona la onda **JUNO DCO** o **JUPITER DUAL** en cualquiera de las 3 capas, ajusta OSC/PWM/VCF, y guarda tu propio preset. Para guardar la combinación con el piano y efectos, usa **STORE** + uno de los ocho botones MAIN.

## Verificaciones realizadas

- `g++ -std=c++17` compiló el motor de synth y el motor de piano aisladamente (comprobación de sintaxis).
- `tests/synth_smoke.cpp`: las ocho configuraciones de fábrica produjeron audio finito/no silencioso a 44.1, 48 y 96 kHz; también se probaron las tres capas con filtro LP/HP/BP, PWM, LFO y pedal sustain.
- Se repitió esa prueba con AddressSanitizer + UndefinedBehaviorSanitizer, sin anomalías detectadas.
- `node --check ui/js/*.js` validó la sintaxis de los JS.
- `python tests/check_integration.py` revisó la correspondencia de los nuevos controles entre UI, parámetros C++ y serialización, además de la integridad de los ocho registros originales.

**Limitación de la verificación:** En este entorno no se pudo realizar la compilación integral del ejecutable con RtAudio/RtMidi/nlohmann-json, ni probar audio/MIDI/OSC en hardware real. Es necesario ejecutar la compilación global y una prueba de sonido en el equipo de destino. El proyecto original usa acceso a parámetros desde el servidor web y el hilo de audio sin una sincronización uniforme; esta actualización no es una auditoría completa de seguridad de tiempo real de todo el host.

## Archivos modificados o agregados

- `src/core/Nordtypes.h`
- `src/dsp/NordDspEffects.h` (capitalización de include)
- `src/loaders/BankLoader.h` (capitalización de include)
- `src/loaders/NsmpLoader.h` (capitalización de include)
- `src/modules/NordSynthModule.h` y `.cpp`
- `src/modules/NordPianoEngine.cpp`
- `src/core/WorkstationHost.cpp`
- `src/main.cpp`
- `ui/js/core.js`, `synth.js`, `sync.js`, `app.js`, `organ.js`
- `ui/synth.html`, `ui/organ.html`, `ui/index.html`
- `ui/css/synth.css` (nuevo)
- `tests/synth_smoke.cpp` y `tests/check_integration.py` (nuevos)
- `ACTUALIZACION_SINTETIZADOR.md` (este documento)
