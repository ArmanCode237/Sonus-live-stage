# Live Stage · Rediseño de interfaz tipo instrumento Nord

**Base:** `Live_Stage_Piano_3_Layers_Organ_Upgrade_COMPLETO.zip` (última versión estable suministrada).
**Alcance:** rediseño de interfaz HTML/CSS/JavaScript; **no se alteró el motor C++, los algoritmos DSP, CMakeLists.txt ni los datos** (`programs.json`, `hardware_config.json`).

## Qué cambió

### MAIN
- Carcasa rojo metalizado, paneles grafito de inspiración Nord, tornillos de marco, rótulos técnicos, botones físicos y LED discretos.
- Ecualizador de cinco bandas con **cinco perillas SVG auténticamente interactivas** en lugar de sliders genéricos. Conservan los IDs del DSP `eqGain0`…`eqGain4` (−15/+15 dB) y siguen sincronizados con el editor EQ completo.
- Volúmenes de los tres layers del piano con **perillas** (`pianoLayerVol0`…`pianoLayerVol2`) en lugar de sliders, conservando los botones L1/L2/L3, encendido y carga por capa.
- Pantalla LCD recreada: programa 01–08, nombre de sonido, número de capas activas de piano/synth y voces activas.
- Debajo de la pantalla, mezclador **PERFORMANCE MIX** de Piano/Synth/Organ: niveles derivados de las ganancias reales y botones POWER conectados a `toggleEffect` (no son decorativos).
- En el mini módulo del órgano, nueve drawbars mecánicos compactos que modifican `drawbar0`…`drawbar8` (0–8 pasos) como los drawbars grandes de la página de órgano.
- Iconografía local SVG sin emojis en la navegación, comandos principales, modales y categorías de banco de sonidos.

### SYNTH
- Cabecera de instrumento con pantalla digital de patch de fábrica.
- Banco físico de presets de fábrica **1–8**, distinto de los **programas globales MAIN 1–8**.
- Tres capas A/B/C con conmutación ON/OFF independiente y visualización de forma de onda.
- Cuatro paneles por capa: **DCO**, **VCF**, **ENVELOPES** y **VOICE**. Todos los parámetros preexistentes permanecen accesibles (OSC A, OSC B, PWM, sub, ruido, drive, filtros, ADSR, unison, wavetable, estéreo).
- Se mantiene la sección MASTER/LFO, presets personalizados del sintetizador y los endpoints existentes.
- Las pestañas afectan **solo la organización visual**, sin activar/desactivar el motor ni cambiar la síntesis por sí mismas.

### ORGAN
- Se conserva el panel detallado de nueve drawbars mecánicos ya existente; se adapta su entorno de interfaz para coincidir con la nueva estética.
- Nuevo módulo compacto de drawbars editables en MAIN con sincronización bidireccional a los mismos parámetros.

## Compatibilidad funcional

- **No se modifican archivos en `src/`.** Los parámetros enviados al servidor siguen siendo los mismos.
- El diseño usa `/css/nord-hardware.css` (nuevo) **al final** del resto de hojas de estilo, para modificar el aspecto sin destruir otros controles.
- `/js/icons.js` y `/js/dashboard.js` son nuevos y se cargan antes de `/js/app.js`.
- Las perillas admiten arrastre vertical con mouse/touch, doble clic para valor predeterminado y ajustes con teclado (flechas, PageUp/Down, Home/End).
- Los controles de capas se sincronizan con la respuesta de `/api/state` y el backend usando los IDs existentes.
- Los ocho programas globales y los bancos independientes de instrumento siguen siendo funciones del mismo backend anterior.
- No se agregan dependencias externas, ni iconos por CDN, ni imágenes remotas.

## Instalación en Windows

1. Haz una copia de seguridad de la carpeta de tu proyecto funcionando.
2. **Recomendado:** extrae `Live_Stage_Nord_UI_SOLO_ACTUALIZADOS.zip` sobre la raíz del proyecto (`CMakeLists.txt` está en esa raíz); permite fusionar `ui/` y `tests/`.
3. **No sobrescribas** tus bancos ni archivos de configuración personales. El ZIP de archivos actualizados **no incluye** `programs.json`, `hardware_config.json` ni `src/`.
4. Inicia Live Stage normalmente y actualiza el navegador con **Ctrl + F5** para evitar archivos CSS antiguos en caché.
5. No es obligatorio recompilar el C++ por estos cambios, ya que el servidor monta `ui/` desde el sistema de archivos. Si prefieres compilar: `cmake --build build --parallel 4`.

## Validación realizada

- `node --check` para todos los scripts de `ui/js/`.
- `python tests/check_integration.py` (conservación de identificadores del sintetizador).
- `python tests/piano_layers_contract.py` (actualizado para perillas en vez de sliders).
- `python tests/nord_ui_contract.py` (nuevo).
- Pruebas en Chromium de la construcción del DOM (MAIN, SYNTH, ORGAN), con 5 perillas EQ, 3 perillas de capa, 9 drawbars MAIN, 3 capas synth, 12 paneles synth y 8 botones de programa.
- Pruebas interactivas aisladas de teclado/knob, drawbar, cambio de pestaña synth, onda, filtro, mute de capa y botones POWER. **Los endpoints fueron simulados en estas pruebas del navegador**, por lo que no prueban salida de audio real.
- Revisiones visuales en resoluciones de 1600, 1280 y 800 px, sin desbordamiento horizontal.

**Limitación:** No se compiló de nuevo el ejecutable Windows con RtAudio/RtMidi ni se hizo prueba de hardware/audio real en este entorno. Como el C++ no fue modificado y el usuario confirmó que la base compila, los cambios están acotados al frontend.
