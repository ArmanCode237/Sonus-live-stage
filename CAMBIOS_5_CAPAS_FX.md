# Live Stage — Correcciones de navegación, piano de 5 capas y matriz de envíos FX

## Resumen

Esta actualización parte del rediseño Nord UI anterior, conserva el motor de sintetizador de tres capas y los bancos de presets. Cambia el motor C++, la interfaz web y la persistencia de los ocho programas globales.

### 1. Corrección de controles desaparecidos al navegar

- El cargador de fragmentos `ui/js/include.js` ya no intenta descargar indefinidamente un fragmento que falló. Ese bucle podía impedir que se inicializaran las perillas, faders y teclado.
- El arranque `ui/js/app.js` inicializa de forma aislada los módulos visuales, de modo que un error en uno no impida dibujar los restantes.
- Al volver desde la memoria de navegación del navegador se restauran las perillas y el teclado solo cuando hace falta.
- Si un fragmento HTML realmente falta en la instalación, se muestra un mensaje de error. Debes copiar todo el ZIP conservando las rutas de carpetas.

### 2. Piano de cinco capas

- Cinco capas de muestras independientes, cada una con ruta de instrumento, interruptor y volumen propio.
- Los sonidos se cargan en la capa que esté seleccionada en el explorador.
- El piano mantiene su interruptor y volumen master.
- El motor mezcla hasta cinco voces de piano por nota si las cinco capas están activas; conviene vigilar la polifonía máxima para acordes complejos.
- Las capas L4 y L5 permanecen apagadas al cargar programas antiguos de tres capas.

### 3. Matriz de envíos AUX FX

Abre **AUX SENDS / FX SENDS** desde el panel MAIN, Synth u Organ. Se muestran nueve filas con tres cantidades por fuente:

| Fuente | Modulation | Delay | Reverb |
|---|---|---|---|
| Piano L1–L5 | 0–100 % | 0–100 % | 0–100 % |
| Synth L1–L3 | 0–100 % | 0–100 % | 0–100 % |
| Organ | 0–100 % | 0–100 % | 0–100 % |

- Los tres efectos son buses paralelos. La señal directa permanece audible aunque todas las cantidades estén en cero.
- El rack global existente sigue definiendo el tipo y carácter del efecto (reverb, delay, chorus/phaser/tremolo). Los controles AUX SENDS definen cuánto de cada **fuente** entra en cada bus.
- Ejemplo: Piano L1 = Mod 0 %, Rev 40 %; Piano L2 = Mod 60 %, Rev 35 %; Synth L1 = Mod 25 %, Rev 70 %.
- Cada perilla modifica un parámetro C++ (`fxSend0Mod` ... `fxSend8Reverb`).
- Los 27 niveles quedan guardados con cada programa global 1–8, y se recuperan al cargarlo.
- Programas antiguos sin matriz AUX conservan sus efectos con 100 % de envío (comportamiento compatible con el mezclador global previo). Usa **RESET AUX SENDS** para restablecer esos valores.
- La matriz no cambia los presets propios del sintetizador y órgano. Se guarda en los ocho programas globales.

### 4. Reducción de clics y golpes

- Órgano: rampa de entrada de aproximadamente 5 ms, reducción del pulso de key click y liberación suavizada.
- Piano: ataque mínimo de 5 ms para evitar discontinuidades en el inicio de samples.
- Mezcla: limitación de picos con transición suave al combinar muchas capas, en lugar de recorte duro.
- Se reservan previamente los buffers de las nueve fuentes al configurar la frecuencia de muestreo, evitando asignaciones por bloque en condiciones normales.
- Nota: estos cambios mitigan clics producidos por envolventes y clipping. No eliminan automáticamente los provocados por buffers ASIO/WASAPI insuficientes, picos de CPU, mala sincronización de drivers o muestras que ya contienen transitorios fuertes.

## Instalación segura en Windows

1. Cierra Live Stage y haz una copia de seguridad de todo el proyecto, en especial `programs.json`, `hardware_config.json` y tus bibliotecas de samples.
2. Extrae **Live_Stage_5_Capas_FX_Actualizados.zip** en la raíz del proyecto, manteniendo estructura de carpetas y reemplazando únicamente los archivos incluidos. El ZIP de actualización no contiene tus configuraciones personales.
3. Cierra y vuelve a abrir la aplicación, actualiza el navegador con **Ctrl+F5**.
4. Como esta versión modifica C++, **sí debes recompilar**:

```bat
cmake -S . -B build -G Ninja
cmake --build build --parallel 4
```

Si Ninja entra en regeneración constante debido a fechas del sistema, puedes eliminar `build` y configurar de nuevo. Los ZIP de esta entrega ya utilizan fechas de archivo normalizadas.

## Comprobaciones realizadas en entorno de desarrollo

- Prueba C++ aislada: 5 capas con sonido, 9×3 envíos, carga de parámetros y muestras finitas (OK).
- Contratos de UI y API preexistentes (OK).
- Prueba de inicio de cada página en Chromium sin backend real: MAIN → Synth → Organ → MAIN (OK; 52 teclas visibles y 9 fuentes FX cada vez; sin excepciones JavaScript).
- Validación sintáctica de los JavaScript de `ui/js` con Node.js (OK).

**Pendiente:** compilación integral del programa completo en Windows MinGW con las dependencias reales y prueba auditiva y de MIDI en tu equipo. Las pruebas de navegador fueron de inicialización visual autónoma, no equivalen a una prueba de servidor y hardware en producción.
