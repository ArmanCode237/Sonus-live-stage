# Archivo de pruebas de la GUI anterior

Estos archivos validaban `ui/index.html`, `ui/js/`, `ui/css/` y otras páginas
HTML que fueron sustituidas por la interfaz React. Se conservan como referencia
histórica, pero **no forman parte de la suite activa**: sin la antigua GUI sus
fixtures y rutas ya no existen.

Pruebas actuales:

- `python tests/check_integration.py` verifica el contrato de React, C++ y los ocho programas.
- `python tools/visual_audit.py` comprueba navegación, 2 × 4 pads, STORE,
  teclado y errores JavaScript en navegador (requiere Chromium + Playwright).
- `cd webui && npm ci && npm run typecheck && npm test && npm run build`
  ejecuta las pruebas React y compila Vite en un equipo con dependencias
  del sistema operativo correctas.
- `ctest --test-dir build --output-on-failure` ejecuta pruebas C++/DSP
  después de una compilación CMake exitosa.

Las pruebas archivadas no demuestran la calidad de la nueva interfaz y no
se deben ejecutar como criterios de aceptación del rediseño.
