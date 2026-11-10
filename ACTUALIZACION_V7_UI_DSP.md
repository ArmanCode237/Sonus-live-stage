# LIVE STAGE V7 — Pantalla, selector de presets y DSP de efectos

Actualización incremental sobre **Live Stage V6.2**. Conserva cinco capas de piano, tres capas de sintetizador, 48 sonidos de fábrica, el órgano y los programas globales.

## 1. MAIN: pantalla de programa

- El **nombre del programa global seleccionado** ocupa el título grande del LCD. Se conserva el número/espacio de programa.
- Debajo aparecen líneas de solo los módulos encendidos, con iconos SVG minimalistas:
  - Piano: nombre del sonido/sample de la primera capa activa y número de capas adicionales si procede.
  - Synth: nombre del preset de fábrica correspondiente al identificador activo.
  - Organ: modelo de órgano activo.
- La pantalla se adapta a resoluciones estrechas. Los módulos apagados se ocultan, no se dejan renglones vacíos.
- El nombre principal lo administra la lógica de programas existente. Se eliminó la escritura antigua del nombre del sample sobre el título principal.

## 2. Selector de presets del sintetizador

- Nuevo explorador con cabecera, foco visual del preset, forma de onda SVG, categorías, filtro y contador.
- Se conservan la selección directa y los botones de los **48 presets** y su confirmación por el motor de V6.2.
- CSS responsive en 1600, 1280, 950, 800 y 600 píxeles; los controles de selección no quedan tapados.

## 3. Chorus global: controles y sonido

Se conservan los parámetros originales y se añaden cinco controles al rack global:

| Control | Efecto |
|---|---|
| Voices | 1 a 3 voces moduladas de chorus |
| Width | Anchura estéreo de la modulación |
| Feedback | Realimentación positiva/negativa acotada |
| Tone | Filtrado tonal de las repeticiones |
| Delay | Tiempo base de retardo del chorus |

Implementación en `src/dsp/NordDspEffects.h`: interpolación de retardo, movimiento entre voces, tratamiento estéreo, filtrado y feedback limitado. Se mantiene compatibilidad con la interfaz anterior y se persisten los nuevos parámetros en programas globales.

## 4. Chorus propio del órgano

Se añade un bloque **Ensemble Chorus** dentro del panel de efectos internos del órgano, independiente del rack global. Dispone de interruptor y siete controles (rate, depth, mix, width, feedback, tone, voices). Procesa únicamente el audio del órgano y permite usar al mismo tiempo los envíos al rack de efectos global. Sus ajustes se conservan en programas globales y en presets propios del órgano.

## 5. Reverbs

Se reorganizan **Room / Plate / Hall** manteniendo las interfaces existentes: pre-delay estéreo, reflexiones tempranas separadas por canal, difusión en cascada, red de retardo con realimentación de ocho ramas, amortiguación, ligera modulación de las colas y supresión de DC. Se suaviza la saturación del motor Shimmer para evitar una transición abrupta en la rodilla del saturador.

**Nota técnica:** son algoritmos nuevos, por tanto un programa antiguo con los mismos valores de Reverb puede escucharse diferente; se conservaron los IDs/parámetros del programa y la semántica de mezcla para respetar el ruteo AUX existente. Se recomienda evaluar a oído el nivel de Reverb y los envíos de efectos de los programas guardados.

## 6. Instalación en Windows

1. Respalda la carpeta del proyecto y los archivos `programs.json`, `hardware_config.json`, tus samples y presets personalizados.
2. Extrae **Live_Stage_V7_SOLO_ACTUALIZADOS.zip** en la raíz de la instalación V6.2 y permite sobrescribir los archivos coincidentes.
3. Detén el ejecutable anterior y configura/compila con Ninja:

```bat
cmake -S . -B build -G Ninja
cmake --build build --parallel 4
```

4. Abre la aplicación y presiona **Ctrl+F5** en la interfaz para limpiar recursos JS/CSS antiguos.
5. Abre el sintetizador y cambia entre distintos presets, confirma el programa global en MAIN, abre FX Rack y el panel de efectos del órgano; verifica que la cola de Reverb no produzca distorsión ni clics con tu dispositivo de audio real.

Los ZIP tienen fechas de archivo normalizadas (8 de noviembre de 2026) para evitar el ciclo de regeneración de Ninja relacionado con marcas de tiempo futuras.

## 7. Comprobaciones realizadas

- JavaScript: validación sintáctica.
- Chromium: selector de 48 presets y filtros en 1600,1280,950,800,600 px.
- Chromium: LCD y ocultación dinámica de instrumentos a 1600,900,600 px; 7 controles del chorus de órgano visibles, interruptor de activación funcional.
- C++: compilación aislada de `NordPianoEngine.cpp` y `NordOrganModule.cpp`.
- DSP aislado 44.1/48/96 kHz: Room, Plate, Hall, chorus y órgano, sin muestras no finitas; pruebas de envolventes, MIDI y 48 presets.
- Sanitizadores de memoria y comportamiento indefinido de la prueba de audio: sin informes de fallos.

**Pendiente:** compilación integral del ejecutable en Windows con las bibliotecas RtAudio/RtMidi y prueba con hardware MIDI/audio real. Las capturas de navegador se generaron con respuestas HTTP simuladas y no prueban por sí solas la integración completa.

## 8. Protección de datos

El ZIP incremental y el ZIP completo **no contienen** `programs.json` ni `hardware_config.json`. No se reemplazan ni eliminan los bancos de muestras personalizados.
