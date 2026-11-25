# Auditoría del motor C++ · Live Stage v32.4

**Objetivo:** dejar la aplicación lista para dos usos: **presentaciones en vivo** (estabilidad, latencia, nada de sorpresas en el escenario) y **estudio** (crear y construir sonidos en sesiones largas).
**Alcance:** motor C++ (`src/`): hilo de audio, MIDI, HTTP/WebSocket, carga de samples, persistencia y comportamiento en Windows.
**Fecha:** 15-nov-2026.

---

## 0. Estado de implementación (v32.4)

Todos los hallazgos están implementados y verificados. Pasan 26 tests C++ (incluido el nuevo `live_safety_v324`), 20 de vitest, 9 smokes y la auditoría de layout (272 / 272). La prueba de resistencia pasa con 0 XRUN y 0 MIDI perdido.

| Id | Implementación | Verificación |
|---|---|---|
| C1 | `SetThreadExecutionState` mientras corre la app (`main.cpp`) | Activo desde el arranque |
| C2 | Vigía de audio en `HardwareManager::checkAudioWatchdog` (stream parado o sin callbacks durante 1.5 s → reabre el mismo dispositivo con *backoff* de 1 a 10 s); aviso «AUDIO REINTENTANDO» en la barra | Revisado en código; **pendiente de prueba física** desconectando la interfaz durante `npm run soak` |
| C3 | `FixedText` (configuración copiable sin memoria dinámica) + `PatchChangeGuard` (fundido de unos 19 ms y configuración congelada mientras se reescribe el patch) en presets, programas, capas y voces DX7 | Test con un hilo de audio concurrente: silencio exacto durante el cambio, el sonido vuelve después |
| A1 | Program Change omni → slot n mod 8, por el hilo de programas | Test del `midiCallback` real |
| A2 | MIDI Clock (24 PPQN, suavizado, 0.1 BPM con histéresis) + START reinicia los secuenciadores; botón **EXT** en el tempo y en SETTINGS | Test a 120 BPM: detectado 120.00; sin EXT el tempo no cambia |
| A3 | Última entrada MIDI en un atómico empaquetado; el mutex de LEARN solo se toma si LEARN está activo | Smokes de mapeos y LEARN |
| A4 | QuickEdit desactivado al arrancar | Activo desde el arranque |
| A5 | `InstrumentPool::trimUnused` (LRU, nunca el setlist ni lo que esté en uso), presupuesto 2 / 4 / 8 / 16 GB en SETTINGS | 3.25 GB fijados por el setlist, dentro de los 4 GB |
| M1 | Seno por tabla (4096 puntos), tanh racional en el drive, `exp2` solo con vibrato o slide, *wrap* de fase sin `floor` | Seno + drive −40 %, supersaw −13 %, FM4 −8 %; sonoridad del banco idéntica (máx. 0.10 dB) |
| M2 | `applySynthPresetJson` transaccional (restaura el patch anterior si el JSON falla a mitad) | Test con un preset malformado |
| M3 | Autoguardado cada 30 s con cambios + archivo de bloqueo; tras un cierre inesperado se ofrece RESTAURAR / DESCARTAR (`recovery.json`) | Corte simulado: se restauró la edición |
| M4 | **PANIC** (botón y parámetro mapeable `panic`), **DSP %** en la barra, telemetría y REINICIAR CONTADORES en SETTINGS | 10 voces sostenidas → 0 |
| B1 | Log con marca de tiempo en `%LOCALAPPDATA%\Live Stage\logs\` (se conservan 14 días) | Archivo creado al arrancar |
| B2 | `npm run soak -- <minutos>` | 2 min: 0 XRUN, 0 MIDI perdido, pico DSP 8 % |
| B3 | Recomendación de 48 kHz en SETTINGS → AUDIO | — |
| B4 | Validación del setlist al arrancar + aviso «N SAMPLES NO ENCONTRADOS» en la barra | Sin faltantes en tu banco actual |

---

## 1. Resumen

El núcleo de audio está en buen estado para el directo:

- el render no bloquea ni reserva memoria;
- los cambios de programa y de efectos hacen fundidos;
- hay un limitador maestro;
- no se encontró ninguna llamada que pueda lanzar una excepción en el hilo de audio.

En una prueba de estrés con piano, synth (supersaw con unison) y órgano a la vez se alcanzaron **60 voces sin un solo corte**, con ASIO a 128 muestras / 44.1 kHz (≈ 2.9 ms).

Los riesgos que quedan **no están en el DSP, sino alrededor de él**:

1. lo que hace Windows durante un concierto largo;
2. qué pasa si se desconecta la interfaz de audio;
3. una carrera de datos rara al cargar presets;
4. funciones MIDI de directo que faltan (Program Change, MIDI Clock).

| Prioridad | Hallazgo | Riesgo en escenario | Esfuerzo |
|---|---|---|---|
| **CRÍTICO** | C1 · Windows puede suspender el equipo o apagar la pantalla | Silencio total a mitad de un tema | Bajo |
| **CRÍTICO** | C2 · Sin recuperación si se pierde el dispositivo de audio | Silencio hasta reiniciar la app | Medio |
| **CRÍTICO** | C3 · Carrera de datos al cargar presets o capas del synth | Cierre inesperado (raro) o un clic al cambiar | Medio |
| ALTO | A1 · No se reciben MIDI Program Change | No se pueden cambiar programas desde la pedalera o el teclado | Bajo |
| ALTO | A2 · Sin MIDI Clock (entrada / salida) | Delays, secuenciador y LFO no siguen a la banda | Medio |
| ALTO | A3 · El hilo MIDI crea un JSON y toma un mutex en cada mensaje | Jitter de notas bajo carga de la UI | Bajo |
| ALTO | A4 · Modo QuickEdit de la consola de Windows | Un clic en la consola congela hilos | Bajo |
| ALTO | A5 · El pool de instrumentos nunca libera RAM | En estudio la memoria crece sin límite | Medio |
| MEDIO | M1 · Senos y tanh exactos por muestra y por voz | CPU de más en FM / osciladores / drive | Medio |
| MEDIO | M2 · Los presets se aplican sin transacción | Un JSON dañado deja un sonido a medias | Bajo |
| MEDIO | M3 · Sin autoguardado del buffer de edición | Se pierde el trabajo de estudio si la app se cierra | Bajo |
| MEDIO | M4 · PANIC y carga DSP no visibles en la UI | Sin salida rápida ni aviso antes de un corte | Bajo |
| BAJO | B1–B4 · Registro a archivo, prueba de resistencia, 48 kHz, validación del setlist | Diagnóstico y prevención | Bajo |

**Recomendación:** resolver **C1, C3, A1 y A4 antes del próximo concierto** (todos con esfuerzo bajo o medio), luego **C2, A3 y M4**. El resto mejora el estudio y el rendimiento.

---

## 2. Mediciones y verificación

| Medida | Resultado |
|---|---|
| Driver / buffer | ASIO (UMC ASIO Driver), 128 muestras, 44.1 kHz → **2.9 ms** |
| Estrés: piano + synth supersaw + órgano, acordes de 10 notas ×3 con pedal | **60 voces**, 0 *underflows*, 0 callbacks > 90 % del presupuesto |
| Tests del motor (C++) | 25 / 25 |
| Tests de interfaz / smokes / auditoría de layout | 20 vitest · 9 smokes · 272 / 272 |

La telemetría ya existe en el motor (`audioUnderflows`, `audioOverBudgetCallbacks`, `maxAudioRenderMicros`, `midiDropped`), pero la interfaz solo muestra XRUN. Ver M4.

---

## 3. Lo que ya está bien (no tocar)

- **Hilo de audio libre de bloqueos.** Los bancos y slots de efectos se publican sin locks (`AudioEpoch`, `RtPublished`), y no hay `new`, E/S ni excepciones en el render.
- **Denormales a cero** (FTZ/DAZ) en cada callback, y **limitador maestro** con retención y *soft-knee* (sin distorsión ni clics).
- **Cambios en vivo sin clics.** Fundidos al encender o apagar módulos, capas, inserts y EQ; las colas de efectos se mantienen al cambiar de programa; *voice stealing* con fundido.
- **Cola MIDI sin bloqueos** entre el callback MIDI y el audio. Los pads de programa solo publican un atómico (`requestedMidiProgram`), así que el hilo MIDI nunca carga de disco.
- **Reconexión automática de MIDI**, guardado atómico de JSON (`saveJsonAtomic`), y httplib 0.65 captura las excepciones de cada ruta (un JSON malformado no cierra la app).
- **Setlist precargado:** `InstrumentPool::preloadSetlistAsync` deja los instrumentos de los programas en RAM, para cambios instantáneos.

---

## 4. Hallazgos críticos

### C1 · Windows puede suspender el equipo o apagar la pantalla durante el show
**Dónde:** no hay ninguna llamada a `SetThreadExecutionState` en `src/`.
**Por qué importa:** Windows no considera el MIDI como actividad del usuario. En un set largo tocando solo con el teclado, el plan de energía (sobre todo en portátil) puede suspender el sistema o apagar la pantalla. El resultado es silencio total.
**Solución:** mientras el stream de audio esté abierto, llamar a `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)`, y liberarlo al cerrar. Opcional: una casilla «Mantener pantalla encendida» en SETTINGS.
**Esfuerzo:** bajo (unas 10 líneas en `HardwareManager`).

### C2 · Sin recuperación si se pierde el dispositivo de audio
**Dónde:** `src/app/HardwareManager.h` abre el stream (`openStream` / `startStream`) y reconecta el MIDI automáticamente, pero **no vigila el audio**.
**Por qué importa:** si se desconecta el USB de la interfaz, el driver ASIO se reinicia o el *hub* pierde energía, el stream se detiene y no vuelve. Hay silencio hasta reiniciar la app, en el peor momento posible.
**Solución:**
1. Añadir un vigía cada 500 ms: si `isStreamRunning()` es falso, o el contador de callbacks no avanza durante más de 1 s, cerrar y reabrir el mismo dispositivo (con reintentos y *backoff*).
2. Si tras N intentos sigue fallando, pasar a un dispositivo de respaldo configurado (por ejemplo WASAPI).
3. Mostrar «AUDIO REINTENTANDO…» en la barra superior y registrarlo en el log.

**Esfuerzo:** medio.

### C3 · Carrera de datos al cargar presets o capas del synth
**Dónde:**
- `StageSynthModule.cpp:1584` copia `layer.runCfg = layer.cfg` **en el hilo de audio** en cada bloque;
- `WorkstationHost.cpp:744/747` hace `layer.cfg = SynthLayerConfig{}` y `:658` asigna `c.samplePath = …` **desde el hilo HTTP**;
- `SynthLayerConfig` contiene un `std::string samplePath` y la voz DX7 completa.

**Por qué importa:** si el hilo HTTP reemplaza el `std::string` mientras el audio lo copia, se lee memoria liberada: es un cierre inesperado, raro pero posible. Hoy la probabilidad es baja, porque casi todas las capas tienen `samplePath` vacío. Además, durante la carga un bloque puede mezclar parámetros viejos y nuevos, lo que se oye como un clic o un transitorio extraño al cambiar de preset.
**Solución (en orden de preferencia):**
1. Publicar la configuración de capas con el mecanismo que ya existe (`RtPublished<…>` + `AudioEpoch`): el hilo de control prepara una copia completa y el audio la toma de forma atómica al inicio del bloque. Así no hay copias parciales ni strings compartidos.
2. Alternativa mínima: sacar `samplePath` del struct que copia el audio (o convertirlo en `std::array<char, 260>`), y hacer el cambio de preset con un *mute* de 5–10 ms (fade-out → aplicar → fade-in).

**Esfuerzo:** medio.

---

## 5. Hallazgos de prioridad alta

### A1 · No se reciben MIDI Program Change (0xC0)
**Dónde:** `AudioCallbacks.cpp::midiCallback` maneja nota, CC, *pitch bend* y *aftertouch*, pero no `0xC0`; `MidiKind` solo tiene `CC` y `Note`.
**Por qué importa:** en directo los programas se cambian desde la pedalera o desde el teclado maestro (por ejemplo el MOXF), no con el ratón.
**Solución:** al recibir `0xC0 n`, publicar `requestedMidiProgram = n % 8`, igual que hacen los pads (sin E/S en el hilo MIDI). Opcional:
- canal de recepción configurable;
- tratar el *bank select* (CC0/CC32) para más de 8 programas;
- un `MidiKind::ProgramChange` para MIDI learn.

**Esfuerzo:** bajo.

### A2 · Sin MIDI Clock (entrada y salida)
**Por qué importa:** con banda o con secuencias, los delays sincronizados, el secuenciador del synth y los LFO deberían seguir el tempo externo.
**Solución:** leer `0xF8` (24 PPQN) con un filtro de suavizado del tempo, y `0xFA/0xFB/0xFC` (start / continue / stop) para el secuenciador. Opción «MASTER CLOCK: INTERNO / MIDI». La salida de clock puede ser opcional.
**Esfuerzo:** medio.

### A3 · El hilo MIDI crea un JSON y toma un mutex en cada mensaje
**Dónde:** `ControlMappings::handleMidi` hace `lastInput_ = {…json…}` bajo `learnMutex_` **en cada nota y cada CC**.
**Por qué importa:** son reservas de memoria por nota y contención con los hilos HTTP/WS que leen ese mutex. No afecta al audio, pero añade *jitter* de entrada cuando la UI está ocupada (por ejemplo al cargar samples).
**Solución:** guardar la última entrada en un `struct` POD con un contador de secuencia (*seqlock*) o atómicos, y construir el JSON solo al leerlo. Tomar `learnMutex_` únicamente cuando `learnActive_` esté activo.
**Esfuerzo:** bajo.

### A4 · Modo QuickEdit de la consola de Windows
**Dónde:** no hay `SetConsoleMode` en `src/`; `HardwareManager` y algunas rutas escriben en `std::cout`.
**Por qué importa:** con QuickEdit activo (el valor por defecto), un clic accidental en la ventana de consola **suspende cualquier hilo que escriba en ella** hasta pulsar una tecla. Si coincide con la reconexión MIDI o con una ruta HTTP, la app parece colgada.
**Solución:** al arrancar, quitar `ENABLE_QUICK_EDIT_MODE` con `SetConsoleMode`, o enviar los logs a un archivo (ver B1). Recomendado: ambas cosas.
**Esfuerzo:** bajo.

### A5 · El pool de instrumentos nunca libera RAM
**Dónde:** `InstrumentPool::getOrLoad` añade entradas a `entries_` y nada las elimina; las muestras se cargan completas en RAM (int16).
**Por qué importa:** en directo esto es ventajoso, porque los cambios son instantáneos. Pero en el estudio, cada instrumento que se prueba en el BROWSER queda en memoria. En una sesión larga con librerías grandes la RAM solo crece, hasta paginar a disco (cortes) o agotarse.
**Solución:**
- liberación LRU con un presupuesto configurable (por ejemplo «Memoria de samples: 4 GB»);
- **nunca** liberar instrumentos usados por las capas activas, los 8 programas o el setlist;
- mostrar el uso de RAM en SETTINGS.

**Esfuerzo:** medio.

---

## 6. Hallazgos de prioridad media (rendimiento y estudio)

### M1 · Senos y tanh exactos por muestra y por voz
**Dónde:** `StageSynthModule.cpp`:
- osciladores tipo seno, *organ harmonics*, *phase warp*, FM2 y FM4 (un `std::sin` por operador, líneas ~736–975);
- drive (`std::tanh` por muestra, línea ~1025);
- transitorios.

**Por qué importa:** son las funciones más caras del bucle interno. Con FM4 (4 operadores × voces × unison) o DX7 + supersaw, ese es el margen que se pierde antes de un corte. El filtro ya usa una aproximación racional de tanh, así que el patrón existe.
**Solución:**
- tabla de seno de 2048 puntos con interpolación lineal (o polinomio de grado 5);
- `fastTanh` racional en el drive;
- coeficientes `std::exp` del *glide* y de las envolventes solo cuando cambian.

**Ganancia esperada:** relevante en los motores FM y de senos; hay que medirla con un benchmark antes y después (se puede añadir como test).
**Esfuerzo:** medio.

### M2 · Los presets se aplican sin transacción
**Dónde:** `applySynthPresetJson` y `applySynthLayerJson` escriben campo por campo. Una excepción de tipos de nlohmann (`get<float>()` sobre un texto) a mitad deja el sonido mezclado. httplib responde 500, pero el estado ya cambió.
**Solución:** deserializar a un `SynthPresetParams` temporal, validarlo y aplicarlo de una sola vez (encaja con la publicación de C3).
**Esfuerzo:** bajo.

### M3 · Sin autoguardado del buffer de edición
**Por qué importa:** en el estudio, si la app se cierra inesperadamente se pierden los cambios no guardados.
**Solución:**
- guardar `edit-buffer.autosave.json` cada 30 s, solo si hubo cambios (el control de «sucio» ya existe por módulo);
- al arrancar tras un cierre inesperado, ofrecer «Restaurar sesión».

**Esfuerzo:** bajo.

### M4 · PANIC y carga DSP no visibles en la UI
**Dónde:** la ruta de *all notes off* existe (`ProgramRoutes.cpp:167`) y `maxAudioRenderMicros` se registra, pero la interfaz no ofrece un botón PANIC ni un indicador de carga.
**Solución:**
- botón **PANIC** en la barra superior (mapeable a un pad o pedal);
- medidor **DSP %** (render medio y pico frente al presupuesto) junto al monitor, en ámbar por encima del 70 % y en rojo por encima del 90 %;
- contador XRUN reiniciable.

**Esfuerzo:** bajo.

---

## 7. Mejoras menores

- **B1 · Registro a archivo** con rotación (`logs/live-stage-YYYYMMDD.log`): arranque, dispositivos, reconexiones, XRUN, errores de carga. Es imprescindible para diagnosticar lo que pasó en un concierto.
- **B2 · Prueba de resistencia automatizada** de 60 minutos: secuencia MIDI de un setlist con cambios de programa cada 30 s, comprobando la memoria, los XRUN y `midiDropped`. Ejecutarla antes de cada gira.
- **B3 · 48 kHz como recomendación** en SETTINGS para interfaces USB: muchos drivers son más estables a 48k, y la latencia baja de 2.9 a 2.7 ms con el mismo buffer.
- **B4 · Validar el setlist al arrancar:** avisar en la UI si algún programa referencia un sample que ya no existe, antes de subir al escenario y no al pulsar el pad.

---

## 8. Plan propuesto

| Fase | Contenido | Resultado |
|---|---|---|
| **1 · Antes del próximo concierto** | C1, A4, A1, C3 (versión mínima: mute de 5–10 ms + `samplePath` fuera de la copia) | Sin suspensiones, sin congelamientos, cambio de programas por MIDI, cambios de preset seguros |
| **2 · Robustez** | C2, A3, M4, B1, B4 | Recuperación automática del audio, MIDI sin jitter, PANIC y DSP % visibles, logs |
| **3 · Estudio y rendimiento** | M1, A5, M2, M3, C3 completo (`RtPublished` de la configuración) | Más polifonía con FM/DX7, RAM controlada, presets atómicos, autoguardado |
| **4 · Integración** | A2, B2, B3 | Sincronía con banda, pruebas de resistencia antes de cada gira |
