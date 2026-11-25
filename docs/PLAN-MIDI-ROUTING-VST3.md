# Plan v33 · Enrutado MIDI por teclado / canal y VST3 con la interfaz completa

## Estado (2026-11-16)
| Fase | Estado | Verificación |
|---|---|---|
| V0 núcleo compartido (`AudioBackend`, `startServices`) | Hecho | todos los smokes del exe sin cambios |
| M0 eventos con fuente y canal | Hecho | `/api/note` y `/api/midi/inject` aceptan `src` / `ch` |
| M1 filtro de Program Change y CC 120/123 | Hecho | `smoke:routing` |
| V1 consola dentro del VST3 (WebView2) | Hecho | `LiveStagePluginHostCheck --editor` |
| V2 estado completo en el proyecto, 8 Programs, SETTINGS en modo plug-in | Hecho | host check (save / restore) |
| M2 tabla de enrutado + note-off por fuente y canal + UI | Hecho | `smoke:routing` |
| V3 automatización (27 parámetros) | Hecho | host check |
| M3 pedal de sustain por ruta (opción «PEDAL POR TECLADO») | Hecho | `smoke:routing` |
| M3b wheel / bend / aftertouch por ruta | Pendiente | siguen siendo globales |
| M4 mapeos limitados a un teclado | Hecho | `smoke:routing` (con el MOXF real) |
| M5 enrutado distinto por Program | Pendiente (opcional) | — |
| V4 pluginval + pruebas en DAWs | Pendiente (requiere tu DAW / pluginval) | — |
| V5 macOS (AU + VST3) | Pendiente (requiere Mac) | — |

Estado de partida: v32.5 (OCT/SEMI por capa, nombre del preset en el motor, nuevo browser).
Regla para todas las fases: **la configuración por defecto se comporta exactamente como hoy**
(todo el MIDI va a todo), así que nada cambia para quien no toque la función nueva.

---

## 1. Cómo funciona hoy el MIDI (análisis)

| Tema | Hoy | Riesgo con 2–3 teclados |
|---|---|---|
| Identidad del dispositivo | Todos los puertos usan el mismo callback con el mismo `userData` (`HardwareManager.h`, `setCallback(&midiCallback, &engine_)`). El motor no sabe de qué teclado viene una nota. | No se puede asignar "teclado 1 → L1". |
| Canal en las notas | Se ignora (omni). `midiCallback` llama `host->noteOn(key, vel)` sin canal. | Todo suena en todos los módulos (solo los splits de KEYZONES los separan). |
| Note-off | Las voces se identifican por la **tecla física** (`v.midiKey == rawMidiKey`). | Si los dos teclados pulsan el mismo Do4, soltarlo en uno corta la nota del otro. |
| Pedal (CC64), wheel, pitch bend, aftertouch | Globales, el último mensaje gana. | El pedal del teclado 2 suelta el sustain del teclado 1; un teclado que envía wheel = 0 "pelea" con el otro. |
| CC 120/123 (all notes off) | Desde cualquier canal / dispositivo. | Un controlador que lo envía al conectarse corta todo. |
| Program Change | Omni: PC n → slot n mod 8. | Unos pads de batería que envían PC en el canal 10 cambian el Program en pleno concierto. |
| Mapeos MIDI | **Sí** tienen en cuenta el canal: un mapeo aprendido guarda el canal (1–16) y solo responde a ese canal; canal 0 = omni responde a todos (`ControlMappings.h`, `MidiSource::matches`). No distinguen dispositivo. | Dos teclados en el mismo canal mueven el mismo mapeo. |
| Concurrencia | La cola MIDI del motor es MPMC sin bloqueos (`pushMidiEvent`, CAS). Varios puertos a la vez **no** corrompen nada. | Ninguno: la base es sólida. |

**Respuesta corta a "¿el mismo mensaje en canal 1 y canal 2 es un riesgo?":** para los mapeos, no
(si el mapeo tiene canal, solo responde a ese canal). Para tocar notas, pedal, wheel y Program
Change sí lo es hoy, porque todo es omni y global.

---

## 2. Enrutado MIDI estilo Reaper (diseño)

### Concepto
- **Fuente** = (dispositivo, canal). Dispositivo = puerto MIDI identificado por su nombre limpio
  (`cleanName`, estable al reconectar); canal = OMNI o 1–16. En el VST3 solo hay un dispositivo
  (la pista del DAW), así que el mismo diseño sirve para "canal 1 → piano, canal 2 → synth".
- **Destinos** = máscara de bits: PIANO L1–L5 · SYNTH A–D · ORGAN (10 bits).
- **Tabla de rutas**: lista de reglas `fuente → destinos`, con opciones por regla:
  transposición de la fuente, rango de teclas opcional, "el pedal / wheel / bend sigue la ruta".
- Por defecto: una regla `TODOS · OMNI → TODO`, idéntica al comportamiento actual.
- Ámbito: **SYSTEM** (depende del hardware conectado), con override opcional por Program
  en una fase posterior.

### Ejemplos que permite
- Teclado 1 (88 teclas) → PIANO L1+L2; teclado 2 (61) → SYNTH A+B; pedalera de bajo → ORGAN.
- Un solo teclado con canal 1 → piano y canal 2 → synth (o desde un DAW).
- Teclado 2 → solo PIANO L3 (un pad en otra capa), con +1 octava propia.

### Arquitectura (sin bloqueos en el hilo de audio)
1. Cada `MidiPortSlot` recibe un `userData` propio `{host, sourceId}`; `sourceId` 0–15
   (0 = interno/teclado en pantalla/OSC, 1–15 puertos).
2. El evento de la cola pasa de `{type, d1, d2}` a `{type, d1, d2, src, ch}` (siguen siendo pocos bytes, sin heap).
3. La tabla de rutas se compila a un array `mask[src][ch]` de 16×17 `uint16_t` y se publica
   con `std::atomic<std::shared_ptr>` / doble buffer, igual que `ControlMappings`
   (la UI edita, el audio solo lee).
4. `handleNoteOnInternal(raw, vel, mask)`: el bucle de capas de piano comprueba el bit de la capa;
   `multiSynth_.noteOn` recibe la máscara de capas A–D; el órgano su bit.
5. **Note-off por fuente**: las voces guardan `src`; el note-off busca `midiKey == raw && src == src`.
   Así el mismo Do4 en los dos teclados no se cortan entre sí. (Mismo cambio para el
   `monoHeldKeys_`, que pasa a indexarse por fuente.)
6. Controladores por ruta (opcional por regla): pedal, wheel, bend y aftertouch se aplican solo a
   los destinos de su fuente. Requiere pasar el sustain de "global" a "por capa" en piano y synth:
   es el cambio más delicado, por eso va en su propia fase.
7. Al desconectarse un dispositivo: note-off de emergencia de **sus** notas (máscara por fuente),
   no de todas.

### Fases (cada una se puede publicar sola)
| Fase | Qué | Riesgo | Pruebas |
|---|---|---|---|
| M0 | Etiquetar eventos con fuente y canal (sin cambiar comportamiento). `/api/note` acepta `src`/`ch` para simular teclados sin hardware. | Bajo | Todos los smokes actuales deben seguir igual. |
| M1 | **Protecciones rápidas**: filtro de canal para Program Change (OMNI / 1–16 / OFF) y opción "ignorar CC 120/123 de otros dispositivos". | Bajo | Smoke con `src`. |
| M2 | Tabla de rutas para notas (máscara por fuente) + note-off por fuente + UI SETTINGS → MIDI ROUTING (matriz dispositivos × destinos, plantillas "Todo a todo", "Teclado 1 piano / Teclado 2 synth"). | Medio | Tests C++ de la tabla; smoke que toca la misma tecla desde 2 fuentes y comprueba que no se cortan. |
| M3 | Pedal / wheel / bend / aftertouch por ruta (sustain por capa). | Medio-alto | Smoke de pedal por teclado; prueba en vivo. |
| M4 | Filtro de dispositivo en los mapeos (`MidiSource` + `port`, 0 = cualquiera) y LEARN que guarda el dispositivo. | Bajo | `mapping-smoke`. |
| M5 | Override de rutas por Program (opcional). | Bajo | Programs smoke. |

---

## 3. VST3 con la interfaz completa

### Situación actual
- `StagePluginProcessor` usa el mismo `WorkstationHost` (DSP idéntico), pero **sin** servidor HTTP,
  sin `StageApp` (programas, presets, biblioteca, mapeos, browser) y con un editor de 2 etiquetas.
- El estado del DAW guarda solo capas de synth y órgano: las muestras de piano, los mapeos y los
  inserts no viajan con el proyecto.

### Enfoque recomendado: la misma UI web dentro del plugin
La UI ya habla solo con REST + WebSocket, así que el plugin puede alojar el **mismo servidor** en
loopback y mostrarla en un `juce::WebBrowserComponent` (WebView2 en Windows, WKWebView en macOS).
Reescribir la UI con componentes JUCE o con el puente nativo de JUCE 8 costaría mucho más y
duplicaría el trabajo para siempre.

### Fases
| Fase | Qué | Notas |
|---|---|---|
| V0 · Preparación (sin cambios visibles) | Separar `StageApp` en **`StageCore`** (motor + datos + rutas + servidor) y dos anfitriones: `StandaloneHost` (RtAudio/RtMidi, ventana nativa, argumentos) y `PluginHost` (audio/MIDI del DAW). Las rutas de hardware consultan una interfaz `AudioBackend` (la del plugin responde "lo gestiona el DAW"). Compilar `StageCore` también con MSVC. | Es el paso que "prepara" la app. Los smokes del exe deben pasar sin cambios. |
| V1 · UI en el plugin | El procesador crea `StageCore`, arranca el servidor en `127.0.0.1:<puerto efímero>` con token de sesión (acceso web forzado a OFF), y el editor carga `http://127.0.0.1:<puerto>/?k=<token>` en `WebBrowserComponent` (`JUCE_USE_WIN_WEBVIEW2=1`, carpeta de datos de WebView2 propia). Editor redimensionable (mín. 1280×720). Si falta el runtime WebView2: mensaje + botón "Abrir en el navegador". | Una instancia = un puerto. Varias instancias funcionan a la vez. |
| V2 · Estado en el proyecto del DAW | `getStateInformation` = el JSON completo del Program (`programJson`: capas de piano con rutas de muestras, synth, órgano, FX, mapeos, `soundRefs`, nombres de preset); `setStateInformation` lo aplica fuera del hilo de audio con carga de muestras asíncrona. Los 8 Programs aparecen como programas del DAW. La UI oculta AUDIO/MIDI DEVICES en modo plugin (`/api/system` → `host: "plugin"`). | Los proyectos guardan y reabren el sonido exacto. |
| V3 · Automatización | ~32 parámetros JUCE (niveles de módulo, macros, rotary, drive, wheel…) enlazados con el `ParameterRegistry`; la automatización del DAW se ve en la UI y viceversa. | IDs estables (no cambiarlos después). |
| V4 · Validación y paquete | pluginval (lo ejecutas tú), pruebas en Reaper / Ableton / Cubase / FL, varias instancias, render offline, cambios de sample rate y de buffer, CPU. Instalador con el .vst3. | — |
| V5 · macOS | Mismo código: AU + VST3, WKWebView, firma y notarización. | Requiere Mac. |

### Riesgos conocidos
- **Datos compartidos**: exe y varias instancias del plugin usan la misma carpeta de usuario
  (presets, biblioteca, `browser_meta.json`). Las escrituras ya son atómicas, pero las cachés en
  memoria pueden quedar desfasadas → V0 añade recarga por fecha de modificación.
- **Render offline**: si el DAW renderiza más rápido que tiempo real, la carga de muestras en
  streaming debe estar completa antes de reproducir (precarga al aplicar el estado).
- **Logic (AU)**: el AU corre en un proceso aislado; hay que verificar que permite loopback (V5).
- **MIDI en el plugin**: el enrutado por dispositivo no aplica (el DAW ya separa pistas); el enrutado
  **por canal** (fase M2) sí, y es lo que hace útil un solo plugin multitímbrico.

### Orden recomendado
`V0 → M0 → M1 → V1 → V2 → M2 → V3 → M3 → M4 → V4 → V5`
(V0 y M0 tocan las mismas piezas —entrada MIDI y anfitrión— y no cambian nada visible; conviene hacerlas juntas.)
