# Live Stage V8 — Presets, filtros, reverb y controles de interpretación

## Diagnóstico de la versión aportada

1. **JavaScript:** `ui/js/synth.js` contenía `consle.log(result)` (typo), una excepción que interrumpía la confirmación de carga aunque el servidor respondiera correctamente.
2. **C++:** `NordSynthModule::loadBuiltInSynthPreset()` hacía `clamp(..., 1, 48)`, aunque la UI y la API aceptaban 120 presets. Los IDs 49–120 terminaban cargando una configuración incorrecta; ahora hay un banco programático diferenciado por familias para 49–120.
3. **Generador:** la UI ofrecía 15 formas de onda, pero varias (Hard Sync, FM, SuperPulse, Ring Mod, Formant, Wavefolder) terminaban en la rama `default` (seno). Se añadieron algoritmos de audio para esos modos. El filtro Notch se comportaba como Low Pass; ahora entrega la salida notch.
4. **Nombres:** `dashboard.js` limitaba la pantalla a 48, por lo que la identificación de presets nuevos en MAIN podía no coincidir. Ahora admite hasta 120.
5. **Categorías 13–48:** el generador usaba familias que no coincidían con el banco etiquetado en la interfaz. Se ajustó para Brass 13–24, Leads 25–36 y Keys 37–48, conservando los presets 1–12.

## Reverb y Shimmer

- **Room, Plate y Hall:** aumento controlado de la señal reverberada, mayor aporte de reflexiones tempranas y de la cola difusa, distinto grado de difusión según el espacio. Se mantiene el contrato del bus AUX wet-only, así que los envíos por capa siguen funcionando sin duplicar la señal seca.
- **Shimmer:** ventanas del pitch-shifter de cuatro granos normalizadas; menor ganancia de las armonizaciones duales y del retorno pitch-shifted; filtrado paso bajo adicional del feedback; menos realce estéreo. Esto limita los armónicos agudos recirculantes y reduce la agresividad en la cola.
- Se precalculan coeficientes del Shimmer cuando cambian los ajustes, para evitar operaciones exponenciales costosas dentro del callback de audio.
- **Importante:** la valoración musical de los cambios requiere una escucha real con tus samples y tu sistema de audio. Son cambios de algoritmos DSP, no una garantía subjetiva de sonido perfecto.

## Controles nuevos del sintetizador

En SYNTH > MASTER / MODULATION:

| Control | Función |
|---|---|
| POLY | Varias notas simultáneas, como antes |
| MONO | Una nota principal, ataque retrigger sin discontinuidad |
| LEGATO | Última nota mantenida tiene prioridad; transición de afinación sin reiniciar envolvente cuando hay superposición |
| PORTAMENTO | Tiempo de transición gradual entre tonos (0–2 segundos) |
| ALWAYS / LEGATO ONLY | Glide en cada nueva nota o solo cuando hay notas superpuestas |
| PITCH SLIDE | Desviación transitoria inicial de -12 a +12 semitonos, que regresa a afinación normal de forma suave |

Los parámetros nuevos se incluyen tanto en los ocho programas completos como en los presets de sintetizador guardados por usuario. Los programas antiguos usan valores compatibles (POLY, Portamento 0, Glide legato-only, Slide 0).

## Pruebas ejecutadas

- Compilación aislada y renderizado de **120/120 presets** y navegación en Chromium a 1600, 1280, 950, 800 y 600 píxeles. Selecciones de 49, 85, 120, 97 desde lista alternativa; filtros de categorías; sin excepciones JS.
- Audio estable para **15/15 osciladores** y filtros LP, HP, BP, NOTCH.
- Audio de legato y portamento monofónico, incluyendo retorno a nota previa en el stack de teclas.
- Pruebas existentes de Room/Plate/Hall a 44.1, 48 y 96 kHz.
- Pruebas adicionales de estabilidad de Shimmer Single/Dual a 44.1, 48 y 96 kHz.
- Validación de sintaxis JavaScript y de sintaxis C++ de `NordPianoEngine.cpp` / `NordSynthModule.cpp`.
- **Pendiente:** compilación y prueba integral de la aplicación Windows con RtAudio/RtMidi y evaluación de audio/MIDI en hardware real. El navegador de pruebas usa una API simulada.

## Instalación

1. Haz respaldo de tu carpeta y, especialmente, de `programs.json`, `hardware_config.json`, `synth_presets.json`, `organ_presets.json` y tus bancos de muestras.
2. Extrae el paquete incremental **SOLO_ACTUALIZADOS** en la raíz de la versión que enviaste, permitiendo reemplazar los archivos del código.
3. Detén las instancias anteriores de Live Stage antes de compilar.
4. Desde CMD, en la carpeta del proyecto:

```bat
cmake -S . -B build -G Ninja
cmake --build build --parallel 4
```

5. Reinicia la aplicación y actualiza la página con Ctrl + F5. En SYNTH > VERIFICAR MOTOR debe aparecer `Live Stage V8 / Factory API 3 (120 audio patches)` y `maxFactoryPresets: 120` en `/api/synth/factory/status`.
6. Prueba el preset **085** y el **120**, una forma de onda FM/Folder y los nuevos controles MONO / LEGATO / PORTAMENTO / SLIDE; escucha cada reverb con un envío AUX medio y sin sobrecargar el MASTER.

Los ZIP no incluyen las configuraciones personales `programs.json` ni `hardware_config.json` y sus entradas usan fechas anteriores al día de instalación para evitar los reintentos infinitos de Ninja que ocurrieron anteriormente.
