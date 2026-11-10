# Live Stage V6.1 — Corrección de selección de presets de sintetizador

## Problema observado

El navegador V6 reconstruía los 48 botones cada vez que recibía una sincronización de parámetros del servidor (`showSynthPreset()` llamaba siempre a `buildSynthPresetButtons()`). Esta operación podía sustituir un botón durante la interacción del usuario y cancelar el clic. Además, la aplicación mostraba «Fábrica: …» sin comprobar si el motor había aplicado realmente el preset; los errores de `sendParam()` quedaban ocultos.

## Correcciones

1. **`ui/js/synth.js`**: actualizar solo el estado visual del botón activo; dejar de reconstruir la lista durante el polling. Las búsquedas y los filtros siguen reconstruyendo la lista cuando corresponde.
2. **`src/main.cpp`**: nuevo endpoint `POST /api/synth/factory/load`, que valida un `id` de 1 a 48, carga el preset, activa el sintetizador en una sola operación y devuelve los parámetros efectivos para confirmar la selección.
3. **`ui/js/synth.js`**: comprobación de respuestas y mensajes de error visibles. Si el ejecutable C++ anterior no tiene el nuevo endpoint, se usa la ruta compatible `/api/param`, verificando igualmente que el número de preset confirmado sea correcto. Las selecciones rápidas se procesan en orden y prevalece la última.
4. **`ui/synth.html`**: actualización de la versión del script para evitar que el navegador reutilice la copia antigua de `synth.js`.
5. **Prueba de regresión**: `tests/synth_factory_regression.py`, con 48 botones, categoría, selección, motor antiguo simulado, confirmación y protección frente a reconstrucción del DOM.

## Instalación en Windows

1. Cierra Live Stage y haz una copia de seguridad del proyecto.
2. Extrae **`Live_Stage_V6_1_CORRECCION_PRESETS.zip`** en la raíz de tu proyecto V6, conservando los subdirectorios. Solo reemplaza `src/main.cpp`, `ui/js/synth.js` y `ui/synth.html`. El archivo `CORRECCION_PRESETS_V6_1.md` y el test son adicionales.
3. Abre CMD en la raíz y ejecuta:

   ```bat
   cmake -S . -B build -G Ninja
   cmake --build build --parallel 4
   ```

4. Reinicia el **nuevo ejecutable**, entra al panel SYNTH y pulsa **Ctrl + F5** para recargar la interfaz.
5. Selecciona un preset de fábrica, por ejemplo **25 — Classic Poly Brass**. Debes ver el número en la pantalla del sintetizador y un mensaje `Cargado: 25 · Classic Poly Brass`.

**Importante:** la actualización incremental no incluye ni sobrescribe `programs.json`, `hardware_config.json`, `synth_presets.json`, `organ_presets.json` ni samples.

## Comprobaciones realizadas

- Probado en Chromium con respuestas de motor simuladas: 48 botones, filtro por categorías, selección por clic, ausencia de reemplazo DOM durante sincronización, confirmación del ID, compatibilidad con el endpoint antiguo y detección de motor antiguo.
- Verificada la sintaxis de `ui/js/synth.js` mediante Node.
- Compilado y ejecutado el test aislado de C++ `tests/v6_engine_smoke.cpp` con los 48 presets.

La compilación completa con RtAudio/RtMidi en **Windows** y la ejecución con el teclado MIDI siguen pendientes de verificación en el equipo real.
