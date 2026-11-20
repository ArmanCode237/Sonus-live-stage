cmake -B build
cmake --build build
.\build\SonusLiveStage.exe ..\LIVE_STAGE_SAMPLES\samples

.\build\SonusLiveStage.exe .\samples






scripts por si hay fechas futuras:

comprueba si hay fechas futuras: 
powershell -NoProfile -Command "Get-ChildItem -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Date) } | Select-Object FullName, LastWriteTime"


corrige las fechas:
powershell -NoProfile -Command "$now=Get-Date; Get-ChildItem -Recurse -File -Force | Where-Object { $_.LastWriteTime -gt $now } | ForEach-Object { $_.LastWriteTime=$now }"




#Compilación de exes instalables del instrumento y vst3:


El VST3 se compiló: está en dist\VST3\Live Stage.vst3. Falta probarlo en un DAW: el validador estándar (pluginval) no pude ejecutarlo, porque el sistema de permisos bloqueó un programa descargado de internet, y yo no tengo un DAW donde abrirlo.

---VST3
Compilación: primera compilación con MSVC (Visual Studio 2022). El motor compiló sin errores; solo faltaba una línea de configuración de JUCE (JUCE_VST3_CAN_REPLACE_VST2=0).

Funciona en cualquier PC: el runtime de Visual C++ va enlazado de forma estática, así que no hace falta instalar el Visual C++ Redistributable.

Lo verificado: el plug-in solo depende de DLL de Windows y su ficha (moduleinfo.json) se generó bien: «Instrument | Synth».

Pruébalo tú en tu DAW:

copia Live Stage.vst3 a C:\Program Files\Common Files\VST3, o ejecuta .\build-vst3.cmd -Install como administrador, y vuelve a escanear los plug-ins;
opcional, con pluginval: pluginval --strictness-level 5 --validate "dist\VST3\Live Stage.vst3".
Recuerda su alcance actual:

motor completo y MIDI;
editor básico, sin la interfaz de la app;
programas con los presets generados por código, no con tu banco de 184.
Llevar la interfaz y el banco al plug-in es el siguiente trabajo.




!!! -Qué necesitas para recompilar tras futuros cambios
No hace falta instalar nada más. Basta con estos comandos, y solo se recompila lo que cambió:


Interfaz (React)   -------->	.\build-exe.cmd
Motor C++  ----------->	.\build-exe.cmd -SkipUi y .\build-vst3.cmd
Ambos  ----------->	.\build-exe.cmd y .\build-vst3.cmd



La versión se cambia en CMakeLists.txt: project(... VERSION x.y.z).


-Clean solo hace falta si actualizas dependencias.
Antes de publicar: tests, smokes con --headless y npm run soak -- 30.
Qué se necesita para macOS (.pkg con app + VST3 + AU)
Un Mac, o compilación en la nube con macOS (por ejemplo GitHub Actions). Desde Windows no se puede compilar ni firmar para Mac.

Xcode, más CMake, Ninja y Node (brew install cmake ninja node).

Apple Developer Program (99 USD/año). Sin la firma Developer ID y la notarización, macOS bloquea la app, el instalador y los plug-ins en otros Macs.

Trabajo en el proyecto:

binario universal (Apple Silicon + Intel);
control de denormales para ARM;
versión Cocoa de la ventana nativa (webview ya usa WKWebView en Mac);
bundle .app con su icono y sus recursos;
script .pkg que instale la app en /Applications y el VST3 y el AU en /Library/Audio/Plug-Ins.
JUCE ya está configurado para generar el AU además del VST3.

Los scripts, CMake y el flujo de GitHub Actions los puedo preparar desde aquí. La compilación, la firma y la prueba final tienen que hacerse en un Mac o en esa nube.

Lo dejé documentado en docs/COMPILAR.md.

¿Quieres que siga con la interfaz completa dentro del VST3, o que prepare la base para macOS?