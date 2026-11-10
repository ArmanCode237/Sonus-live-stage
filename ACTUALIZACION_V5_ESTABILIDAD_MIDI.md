# Live Stage V5 — MIDI, configuración por capa, efectos de órgano y matriz AUX

Base: `Live_Stage_5_Capas_FX_Proyecto_Completo.zip` (última versión entregada).

## Cambios incluidos

### 1. Corrección de AUX SENDS (27 controles)
- Las nueve filas (cinco Piano, tres Synth y un Organ) y sus 27 perillas ahora existen en `ui/partials/modal-fx-sends.html`, sin depender de que JavaScript genere el contenido del modal.
- `ui/js/fx-sends.js` reconoce la matriz estática y evita volver a escribirla.
- `ui/js/core.js` inicializa y sincroniza las perillas al abrir el modal. Los controles siguen enviando `fxSend0Mod` ... `fxSend8Reverb` al motor.
- Importante: los envíos generales no cambian los efectos internos del órgano.

### 2. SAMPLE CONFIG de L1–L5
- Botón `SAMPLE CONFIG · ADSR` bajo las capas de piano en MAIN.
- Ventana nueva con pestañas por capa y siete perillas independientes: **Attack, Decay, Sustain, Release, Loop Hold, Pan, Fine Tune**.
- El DSP de piano usa los parámetros de la capa de cada voz en el audio real, no solo en la interfaz.
- Los parámetros quedan guardados en `params.pianoSampleConfig[]` de cada programa global (8 memorias).
- Los programas anteriores sin `pianoSampleConfig` recuperan la envolvente global antigua como valores iniciales.
- Los controles anteriores de envolvente global del piano conservan un efecto sobre las cinco capas cuando se manipulan (edición global).

### 3. MAIN más compacto
- La lista de las cinco capas tiene scroll vertical con altura máxima responsive, sin estirar las otras columnas del panel.
- Se conserva la carga de bancos por capa y los controles de volumen existentes.

### 4. Organ FX independiente
- Botón `ORGAN FX` en MAIN y `ORGAN FX · EDIT` en la página Organ.
- Rotary MIX (mezcla entre señal directa y Leslie), velocidad SLOW/FAST, scanner Vibrato/Chorus DEPTH y RATE, saturación DRIVE y percusión (decay/level).
- Rotary, scanner y drive se procesan en el módulo de órgano **antes** del AUX rack maestro.
- Estos valores se incluyen en programas globales y presets propios de órgano.
- Reverb y Delay siguen siendo buses compartidos controlados mediante la matriz de envíos; no se crearon instancias exclusivas de reverb/delay para el órgano.

### 5. Controladores MIDI en vivo
- MIDI CC1: rueda de modulación, normalizada 0–127 (sintetizador: vibrato, órgano: vibrato, piano: modulación suave de amplitud).
- Mensajes Pitch Bend `0xE0`: interpretación de 14 bits, centro 8192 y rango de **±2 semitonos** en los tres motores.
- CC121: restablece ambas ruedas a valores neutros.
- Los valores de las ruedas son temporales (no se guardan como presets) y se muestran en MAIN.
- Los controles respetan la entrada MIDI de cualquier canal, como hace la recepción de notas de esta versión.

### 6. Banco adicional del sintetizador
Se mantienen 1–8 y se añaden:
9. Analog Keys
10. Glass Bells
11. Motion Pad
12. Vintage Brass

Los cuatro nuevos programas se generan por C++ y son editables como los anteriores. El módulo Synth mantiene **exactamente tres capas**. Los ocho programas globales de MAIN no se convierten en 12: siguen siendo ocho.

### 7. Categorías de la biblioteca
La biblioteca incluye nuevas categorías manuales y heurísticas por nombre de archivo, sin sobreescribir `sample_tags.json`: Pads & Atmospheres, Synth Leads, Bass & Sub, Plucks & Arpeggios, Bells & Chimes, Brass, Strings, Choirs & Vocals, Guitars & Plucked, Drums & Percussion, Ambient & Textures y Sound Effects. Se conservan las categorías anteriores.

## Instalación (Windows / CMD)
1. Cierra Live Stage y haz copia de seguridad del proyecto, `programs.json`, `hardware_config.json`, `sample_tags.json`, `synth_presets.json` y `organ_presets.json` si existen.
2. **Paquete incremental:** extrae el contenido ZIP en la raíz del proyecto preservando carpetas. No se incluyen archivos de configuración personales.
3. Recompila, porque el C++ ha cambiado:

```bat
cmake -S . -B build -G Ninja
cmake --build build --parallel 4
```

4. Reinicia la aplicación y recarga la interfaz con Ctrl+F5.
5. Abre MAIN → ENVÍOS POR CAPA: comprueba las 27 perillas. MAIN → SAMPLE CONFIG: comprueba siete parámetros por cada una de cinco capas. Organ → ORGAN FX. Synth → presets 09–12.

Los ZIP usan marcas de tiempo normalizadas a una fecha pasada para evitar la regeneración repetida de CMake causada por archivos con fecha futura.

## Verificación / limitaciones
- Compilación y ejecución de pruebas aisladas del motor de audio: **superadas**.
- Pruebas de per-layer ADSR, recuperación de parámetros, modulación/bend, 12 presets y valores del órgano: **superadas**.
- JavaScript: validación de sintaxis (`node --check`): **superada**.
- Contratos de interfaz: 27 knobs AUX, 35 de sample, 6 de organ: **superados**.
- Compilación completa de Windows con RtAudio/RtMidi: **pendiente de verificar** en tu entorno.
- La navegación visual con navegador real no pudo comprobarse debido al bloqueo de navegación del entorno de pruebas.
- La ejecución con sanitizadores agotó el tiempo disponible; queda pendiente.

## Arquitectura
El C++ modifica `NordPianoEngine`, `NordSynthModule`, `NordOrganModule`, `WorkstationHost`, `Nordtypes` y la gestión MIDI/serialización de `src/main.cpp`. El resto del pipeline de samples, presets independientes y rack existente sigue en su lugar.
