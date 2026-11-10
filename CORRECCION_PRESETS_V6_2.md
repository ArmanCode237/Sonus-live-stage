# Live Stage V6.2 — corrección real del selector Factory Synth

## Problema encontrado

El área `.synth-factory-bank` era un `display:flex` horizontal y contenía, en la misma fila, el encabezado, los filtros de categoría/búsqueda y la cuadrícula de 48 presets. Al crecer estos elementos, el ancho disponible para `.synth-preset-buttons` podía reducirse drásticamente: en una reproducción con el panel auxiliar de verificación añadido, se midió **solo 6 píxeles de ancho**. Los 48 botones estaban presentes, pero se colocaban fuera del área interactiva y los clics eran interceptados por los contenedores circundantes.

Los tests de V6.1 utilizaban principalmente botones con HTML aislado/respuestas simuladas y no ejercitaban el diseño completo con los elementos auxiliares. Por ello, pasar un test de JavaScript no descartaba un fallo de CSS.

## Archivos modificados

- `ui/css/synth.css`: el banco de fábrica ahora utiliza secciones verticales de ancho completo, cuadrícula con scroll interno, anchura mínima controlada y controles clicables en pantallas estrechas.
- `ui/synth.html`: selector desplegable nativo adicional, botón `VERIFICAR MOTOR`, indicador de conexión y versionado de todos los scripts para evitar mezclas de JavaScript antiguo y nuevo.
- `ui/js/synth.js`: sincroniza la selección entre botones y desplegable; comprueba que el servidor haya cargado el programa antes de confirmarlo; consulta el motor activo vía `GET /api/synth/factory/status`; muestra si un motor antiguo ocupa el puerto.
- `ui/js/app.js`: consulta la versión del motor al abrir el panel SYNTH.
- `src/main.cpp`: agrega `GET /api/synth/factory/status` (API 2), devuelve datos reales del sintetizador y agrega cabeceras para evitar caché obsoleta. La operación `POST /api/synth/factory/load` sigue cargando el patch y encendiendo SYNTH.
- `tests/synth_factory_browser_v62.py`: prueba el *clic real* con todos los CSS y JS de producción a 1600, 1280, 950, 800 y 600 píxeles, con API simulada. También prueba 48 botones, el desplegable, el filtro y la confirmación.
- `tests/synth_factory_audio_v62.cpp`: verifica que los 48 patches producen audio finito y distinto de silencio con el motor C++ real, sin controlador de audio externo.

## Diagnóstico del ejecutable que está atendiendo en Windows

Con Live Stage V6.2 en ejecución, abre en el navegador:

    http://localhost:8080/api/synth/factory/status

El JSON debe mostrar valores como `"apiVersion":2`, `"maxFactoryPresets":48`, `"build":"Live Stage V6.2 / Factory API 2"` y `"presetId":25` (el número cambia según el sonido actual).

**Si ves HTTP 404**, la aplicación abierta en el puerto 8080 no corresponde al ejecutable actualizado o está usando una versión anterior.

En CMD puedes revisar qué proceso ocupa el puerto:

    netstat -ano | findstr :8080
    tasklist /FI "PID eq NUMERO_PID"

Cierra la instancia que esté usando 8080 antes de iniciar el ejecutable nuevo; no finalices procesos sin comprobar primero el PID.

**Si el motor marca `synthEnabled:false` o `synthMasterVol:0`**, no se escuchará el sintetizador aunque el preset exista. El nuevo panel muestra también estos diagnósticos, incluido el número de capas activas.

## Instalación

1. Cierra el proceso Live Stage que se esté ejecutando y haz copia de seguridad.
2. Para corregir solo la disposición de botones, extrae `Live_Stage_V6_2_SOLO_CSS.zip` sobre la raíz de V6.1: **no requiere recompilación C++**.
3. Para incluir el verificador de versión, el selector alternativo y la comprobación real de carga, extrae **`Live_Stage_V6_2_CORRECCION_COMPLETA.zip`** sobre la raíz de V6.1 y ejecuta:

       cmake -S . -B build -G Ninja
       cmake --build build --parallel 4

4. Inicia el **nuevo** `build/NordVirtualStage.exe`, abre `http://localhost:8080/synth` y usa `Ctrl+F5`.
5. Pulsa `VERIFICAR MOTOR` y prueba, por ejemplo, `13 Neo Poly Keys`, `25 Classic Poly Brass` y `48 Stadium Saw`.

Los ZIP de actualización no contienen `programs.json`, `hardware_config.json`, `synth_presets.json`, `organ_presets.json` ni tus bancos de samples. Conserva sus copias actuales.

## Pruebas realizadas y límites

- Prueba de clic real en Chromium con CSS completo a 1600/1280/950/800/600 px: **correcto**.
- Selector nativo, filtro por categoría y confirmación con API simulada: **correcto**.
- Prueba aislada del motor C++ con 48 presets y renderizado de audio: **correcto**.
- Validación sintáctica de JavaScript: **correcta**.
- Compilación y operación integral en Windows con RtAudio/RtMidi: **pendiente**, porque no está disponible el entorno de ejecución/teclado físico del usuario.

No confundir las pruebas del navegador con API simulada con una confirmación de la instancia de Live Stage que está ejecutándose en tu PC. El diagnóstico de V6.2 está precisamente para distinguirlas.
