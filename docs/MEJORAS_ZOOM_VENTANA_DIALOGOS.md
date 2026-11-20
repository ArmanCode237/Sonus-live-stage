# Live Stage — Zoom / Aspect ratio / Dialogos nativos (Windows + macOS)

Cambios implementados en el código fuente React y la ventana standalone C++.
No modifican los modulos de audio DSP ni los programas/presets.

## Comportamiento

- En **SETTINGS > SYSTEM > ZOOM UI**, seleccionar: 65, 75, 85, 90, 100, 110 o 125 %.
  El valor inicial es 100 %, y se guarda en `localStorage` (preferencia por interfaz/equipo).
  Restablecer = 100 %. La implementacion usa CSS `zoom` nativo, no transform de los controles.
- La consola conserva su diseño 16:10; al entrar en un viewport diferente (pantalla completa,
  navegador u host plugin), se centra con margenes vacios sin distorsionarse.
- Windows standalone: la ventana mantiene 16:10 en **el contenido** durante el arrastre
  de bordes/esquinas, se reajusta al area util de la pantalla al iniciar, se puede hacer
  mas grande/chica. Maximizar normal queda deshabilitado porque rompería la proporcion.
  F11 sigue permitiendo pantalla completa con letterboxing.
- macOS standalone: la ventana usa `NSWindow.contentAspectRatio = 16:10`, tamaño minimo
  800x500 puntos, mantiene resize libre dentro de esa proporcion. La UI se ajusta con
  letterboxing cuando el sistema/host no pueda mantener el aspecto.
- LIBRARY > SAMPLE SOURCES: aparece `EXPLORAR...` junto a `CARPETA` / `ARCHIVO`, abre
  `IFileOpenDialog` en Windows y `NSOpenPanel` en macOS. También aparece para seleccionar
  la carpeta de CACHE PCM y la nueva carpeta en REPARAR REFERENCIAS.
- La ruta seleccionada es real y absoluta, validada despues por el endpoint C++ existente.
  Cancelar la ventana no altera el campo. Los campos de ruta manual siguen disponibles.
  En el navegador de terceros el boton no aparece: no hay acceso fiable a rutas absolutas
  del sistema mediante HTML `<input type=file>`.

## IMPORTANTISIMO: recompilar la interfaz

La carpeta `ui/app/` se conserva desde el ZIP original como **fallback**, pero se debe
reconstruir con Node/npm para incorporar las mejoras React. Este entorno de prueba no
pudo construir Vite por traer `node_modules` de Windows (Rollup y esbuild de Windows).
El paquete fuente NO incluye `node_modules`, por seguridad y portabilidad.

Windows (PowerShell / terminal, raiz del proyecto):

    cd webui
    npm ci
    npm run build
    cd ..
    .\build-exe.cmd

Si utilizas un script que no compila la UI, primero ejecuta los tres comandos npm.
Para VST3, reconstruye `webui` antes de ejecutar `build-vst3.cmd`.

macOS (terminal en la raiz):

    chmod +x scripts/build_macos.sh
    ./scripts/build_macos.sh

El script macOS recompila React, el standalone, VST3 y AU; despues genera el .pkg.
No uses `--skip-ui` la primera vez, porque conservaria el bundle React antiguo.

## Cambios CMake

El ZIP suministrado incluia `CmakeLists.txt` (capitalizacion no estandar) con numerosos
bloques duplicados y sin `LANGUAGES C`. Se normalizo a `CMakeLists.txt` y se retiraron
los duplicados para permitir la configuracion del proyecto (validado con CMake 3.31
sin dependencias de produccion). Se añadió `NativeFileDialogMac.mm` solo en Apple
cuando `STAGE_NATIVE_WINDOW=ON`; requiere AppKit (integrado en macOS).

## Estado de las verificaciones

- TypeScript: `npm run typecheck` PASO.
- CMake: configuracion sin plugin/nativa (Linux) PASO con stubs de dependencias;
  opcion de tests ON configuró sin errores.
- macOS/Windows: no hay Xcode ni MSVC/MinGW en este entorno. PENDIENTE validar
  la construccion nativa y hacer prueba de la ventana y los exploradores en cada SO.
- React/Vite: empaquetado nuevo PENDIENTE en Win/Mac (el ZIP contiene UI anterior).
- Audio/DSP: sin archivos modificados; no se repitio una prueba de audio.

## Pruebas manuales recomendadas

1. Arrancar standalone y cambiar 100 => 75 => 125 => restablecer, cerrar/reabrir.
2. Achicar la ventana por cada borde/esquina y comprobar relacion 16:10.
3. F11 y comprobar margenes, paneles y modales.
4. En LIBRARY seleccionar una carpeta con samples y registrar; repetir con un archivo
   `.npno/.nsmp4/.sf2/.sfz`. Cancelar sin cambiar el texto.
5. Comprobar rutas Unicode/con tildes o espacios en ambos SO.
6. Probar un preset Piano, un Synth, un Organ y MIDI para detectar regresiones.
