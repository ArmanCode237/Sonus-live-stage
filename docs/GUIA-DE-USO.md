# Sonus Live Stage · Guía de uso

Sonus Live Stage es un instrumento de escenario con tres módulos que suenan a la vez:
**PIANO** (hasta 5 capas de muestras), **SYNTH** (4 capas: analógico, wavetable, FM, DX7, MT-32 y LA32)
y **ORGAN** (drawbars, percusión y rotary). Funciona como aplicación de Windows y como plug-in VST3 dentro de tu DAW.

> Para compilar la aplicación consulta [COMPILAR.md](COMPILAR.md). Licencia y componentes de terceros: [NOTICE.md](../NOTICE.md).

---

## 1. Instalación

**Aplicación (Windows 10 / 11)**
1. Descomprime `SonusLiveStage-<versión>-win64.zip` en una carpeta, por ejemplo `C:\Live Stage`.
2. Ejecuta `SonusLiveStage.exe`. Se abre la ventana de la consola.

Tus datos (Programs, presets, mapeos, enrutado, logs y autoguardado) se guardan en
`%LOCALAPPDATA%\Live Stage`, nunca junto al ejecutable. Puedes actualizar la app sin perderlos.

**Plug-in VST3**
1. Copia la carpeta `Sonus Live Stage.vst3` a `C:\Program Files\Common Files\VST3`.
2. Vuelve a escanear los plug-ins en tu DAW (Reaper, Cubase, Ableton Live, Studio One…).
3. Insértalo como **instrumento** en una pista MIDI.

El plug-in usa los mismos datos que la app: los Programs, presets y la biblioteca de muestras se comparten.

**Requisitos:** Microsoft Edge WebView2 Runtime, que ya viene con Windows 10 / 11. Las muestras de
instrumentos y las ROMs no se incluyen: tú aportas las tuyas (ver la sección 12).

---

## 2. Primeros pasos

1. **Audio:** en **SETTINGS → AUDIO ENGINE**, elige tu interfaz.
   - Usa **ASIO** si tu interfaz lo tiene, o **WASAPI** si no.
   - Elige la frecuencia de muestreo y el buffer. 128 o 256 muestras es un buen punto de partida en directo.
2. **Teclado MIDI:** en **SETTINGS → MIDI INPUTS**, activa tu teclado.
   - El indicador **MIDI** de la cabecera se pone verde.
   - Si lo desconectas y lo vuelves a conectar, Live Stage lo reabre solo.
3. **Muestras:** en **LIBRARY → SAMPLE SOURCES**, elige **CARPETA** y escribe la ruta de la carpeta con tus muestras (por ejemplo `D:\Samples`). Pulsa **AÑADIR**.
   - La primera lectura convierte las muestras a una caché PCM y puede tardar.
   - A partir de ahí, las cargas son rápidas.
4. **Prueba:** pulsa **KEYS** en la cabecera para mostrar el teclado en pantalla, o toca tu teclado.

**Cabecera (siempre visible)**

| Control | Qué hace |
|---|---|
| PERFORM · PIANO · SYNTH · ORGAN · FX/MIX · ZONES · LIBRARY · SETTINGS | Páginas |
| MIDI · DSP · LIVE | Teclados conectados, carga de CPU del audio y estado del motor |
| BPM · EXT · TAP | Tempo maestro: escribe el valor, sigue el **MIDI CLOCK** externo (**EXT**) o marca el pulso (**TAP**) |
| **PANIC** | Corta todas las notas, suelta el pedal y centra el pitch bend |
| **MAP** | Modo de mapeo: resalta los controles que se pueden asignar a MIDI / OSC |
| **KEYS** | Muestra u oculta el teclado de 88 teclas en pantalla |
| **Logo** | Abre **ACERCA DE**: versión, licencia, créditos, tu carpeta de datos y el botón **APOYAR** (se abre en tu navegador) |

---

## 3. PERFORM: la página para tocar

PERFORM reúne todo lo necesario en directo. Para la edición profunda, cada módulo tiene un **EDIT ↗** que abre su página.

**PIANO (izquierda)**
- **Instrumentos:** el selector de categoría con **◀ ▶** recorre los instrumentos, y **BROWSER** abre el buscador (sección 4).
- **Capas L1–L5:**
  - el LED de cada capa la enciende o apaga;
  - pulsa el nombre para cargar otro instrumento;
  - **×** vacía la capa y la perilla controla su nivel;
  - debajo del nombre aparecen su rango de teclas y, si existe, su desplazamiento (p. ej. `+1 OCT -3 ST`).
- **KEYZONES** muestra el reparto de teclas y **QUICK EDIT** los ajustes rápidos de la capa seleccionada.

**Centro**
- **Pantalla del Program:**
  - nombre del Program;
  - qué suena en cada módulo, incluido el nombre del preset de synth u órgano cargado;
  - capas activas y voces sonando.
- **MASTER:**
  - nivel de cada módulo y nivel MASTER;
  - **TRANSPOSE** (±12) y **OCTAVE SHIFT** (±2) globales;
  - respuesta del teclado (**LIGHT / NORMAL / HEAVY**) y **SOFT REL**.
- **PROGRAM MEMORY · 8 SLOTS:**
  - pulsa un pad para recuperar ese Program;
  - para guardar: **STORE** y después el pad de destino;
  - **INIT** reinicia el sonido actual sin borrar ningún Program.

**SYNTH (derecha arriba):** categoría con **◀ ▶**, **BROWSER**, **★** (favorito) y las capas **A–D**. Las perillas son **LEVEL** y **TIMBRE**.

**ORGAN (derecha abajo):** **BROWSER**, **ROTARY** (SLOW / FAST), **LEVEL**, **DRIVE** y los 9 drawbars.

> Un **Program** guarda todo: capas de piano con sus muestras, synth, órgano, efectos, zonas y sus propios mapeos.
> Si cambias algo y no lo guardas con STORE, el indicador de cambios sin guardar te lo recuerda.

---

## 4. El buscador de sonidos (BROWSER)

El mismo buscador sirve para piano, synth y órgano. Se abre desde **BROWSER** en PERFORM o en la página del módulo.

- **Columna izquierda (filtros):**
  - **ORIGEN:** fábrica, editado, usuario, DX7, D-50, MT-32, biblioteca, Sounds, performances;
  - **★ FAVORITOS** y **⟲ RECIENTES**;
  - **CATEGORÍAS**, **MOTOR / FORMATO** y **ETIQUETAS**.
- **Búsqueda:**
  - cada palabra debe aparecer, y no importan los acentos ("organo" encuentra "Órgano");
  - `#etiqueta` busca una etiqueta exacta;
  - **ORDEN:** número, nombre, categoría, recientes, más usados u origen;
  - **vistas:** lista, agrupada por categoría o tarjetas.
- **Cargar sin riesgos en directo:**
  - un clic solo selecciona;
  - **doble clic**, **Enter** o **CARGAR** cargan el sonido;
  - **PRE-ESCUCHA** (apagada por defecto) carga mientras te mueves con las flechas.
- **Panel de detalles:**
  - cambiar la categoría y añadir etiquetas (también a los presets de fábrica);
  - elegir la capa de destino (A–D o L1–L5);
  - **★ FAVORITO**;
  - y, en los presets de usuario, **RENOMBRAR**, **DUPLICAR** y **BORRAR**.
- **Teclas:**
  - **↑ ↓** mueven la selección;
  - **/** va a la búsqueda;
  - **F** marca favorito;
  - **Esc** cierra.
- **FUENTES** (solo synth): **DX7 · CARTUCHOS…** importa o borra cartuchos `.syx`, y **MT-32 · ROMS…** muestra el estado de las ROMs.

---

## 5. PIANO

- **LAYER MIX:** nivel, encendido y selección de las 5 capas.
- **SAMPLE EDITOR:** el sonido de la capa seleccionada:
  - envolvente, afinación fina, filtro y ganancia;
  - rueda de modulación (cutoff, volumen, vibrato);
  - EQ propio, modo **MONO / LEGATO / PORTAMENTO** e inserts.
- **SOUNDS:** guarda el ajuste de una capa como un *Sound* reutilizable, con nombre y etiquetas.
- **PIANO PERFORMANCES:** guarda y carga combinaciones de las 5 capas.

---

## 6. SYNTH

- **Barra de patch:**
  - **◀ ▶** recorren los 184 presets de fábrica;
  - **BROWSER** abre el buscador y **★** marca favorito;
  - **GUARDAR** crea un **preset nuevo de usuario** o guarda la edición sobre el preset de fábrica (el original se puede restaurar);
  - el nombre del preset de usuario aparece en esta pantalla y en PERFORM, también tras reiniciar o recuperar un Program.
- **Capas A–D:** cada capa tiene su propio motor:
  - **SÍNTESIS**: sustractivo, wavetable, FM de 4 operadores, **DX7**, **MT-32**, LA32…;
  - oscilador, filtro y envolventes.
- **Pestañas inferiores:**
  - **LFO** y **MATRIZ** (modulación);
  - **EXPRESIÓN** (rueda, aftertouch, transitorios);
  - **VOICE** (poly / mono / legato, glide);
  - **SEQ** (secuenciador de 16 pasos) e **INSERTS** (efectos de la capa, EQ).

---

## 7. ORGAN

- **Modelo** de órgano y registros rápidos (**FULL, JAZZ, GOSPEL, ROCK, BALLAD, FLUTE**).
- **DRAWBARS**: 9 tiradores (16' … 1'). Haz clic o arrastra sobre cada uno.
- **VIBRATO / CHORUS**, **PERCUSSION**, **ROTARY** (SLOW / FAST) y **ENSEMBLE CHORUS**.
- **USER PRESETS** guarda tus registros, y **BROWSER** los busca junto a los de fábrica.

---

## 8. ZONES: splits, capas y transposición

Cada módulo tiene su rango de teclas, y cada capa (piano L1–L5, synth A–D) el suyo dentro del módulo.
- **LOW / HIGH:**
  - **« − + »** mueven el límite;
  - **LEARN** lo fija con la siguiente tecla que toques (**Esc** cancela).
- **OCT / SEMI:** octava (±2) y semitonos (±12) **de cada capa**, que se suman al TRANSPOSE global.
  - La cápsula se ilumina en ámbar cuando la capa está desplazada, y **0** la devuelve a su sitio.
  - En el synth, OCT / SEMI forman parte del preset.
- Una capa nunca suena fuera de la zona de su módulo: la parte que queda fuera se ve rayada.

**Ejemplo, bajo a la izquierda y piano a la derecha:**
- piano L1 de C3 a C8;
- synth A de C0 a B2 con OCT −1.

---

## 9. FX / MIX

- Racks de efectos por módulo (**CHORUS, PHASER, FLANGER, TREMOLO, DELAY, REVERB**) y sus envíos (**SENDS**).
- **MASTER COLOR** (EQ, drive, autopan del máster).
- El **DELAY** puede sincronizarse al tempo maestro, y el **REVERB** incluye un modo *shimmer*.

---

## 10. Varios teclados y canales: MIDI ROUTING

En **SETTINGS → MIDI ROUTING** decides qué toca cada teclado o canal, como en Reaper.

1. Activa el enrutado (**DESACTIVADO** → **ACTIVADO**). Desactivado, todo el MIDI toca todo, como siempre.
2. Elige una plantilla:
   - **TODO A TODO**;
   - **CANAL 1·2·3 → PIANO·SYNTH·ORGAN**;
   - **TECLADO 1 → PIANO · TECLADO 2 → SYNTH** (aparece si tienes dos o más teclados activos).
3. O crea reglas a mano con **+ REGLA**: **ENTRADA** (un teclado o *cualquiera*), **CANAL** (OMNI o 1–16) y **DESTINOS**:
   - **PIANO** con sus capas L1–L5;
   - **SYNTH** con A–D;
   - **ORGAN**.

Detalles para tocar en directo:
- Las notas que ya suenan terminan con el enrutado con el que empezaron: cambiar reglas nunca deja notas colgadas.
- La misma tecla en dos teclados no se corta entre sí.
- **PEDAL POR TECLADO** (activado por defecto): el sustain de cada teclado sostiene solo sus capas. Apágalo si quieres que un pedal sostenga todo.
- El teclado en pantalla (KEYS) siempre toca todo.
- La rueda de modulación, el pitch bend y el aftertouch siguen siendo globales: afectan a todos los módulos.
- Si una entrada no tiene ninguna regla, el panel avisa «Sin regla (no suenan)».

---

## 11. Mapear controles MIDI y OSC

1. Pulsa **MAP** en la cabecera: los controles mapeables se resaltan.
2. Haz **clic derecho** sobre una perilla, botón o fader y pulsa **APRENDER**.
3. Mueve el control de tu teclado o envía el mensaje OSC.

Opciones de cada mapeo:
- **Canal:** OMNI o 1–16. Un mapeo con canal solo responde a ese canal.
- **Teclado:** «cualquier teclado» o uno concreto. Es útil si dos teclados envían el mismo CC.
- **Ámbito**, del que más manda al que menos:
  - **PROGRAM** (va con el Program);
  - **MODULE** (va con el preset del módulo);
  - **SOUND** (va con el Sound de una capa de piano);
  - **SYSTEM** (siempre activo).
- **Toma de control:**
  - **PICKUP**: el parámetro no salta hasta que el control alcanza su valor;
  - **JUMP**: salta en cuanto mueves el control;
  - **RELATIVE**: para encoders.
- **Rango:** mínimo, máximo e invertir.
- **Botones:** continuo, **TOGGLE** o **MOMENTARY**.

Algunos parámetros se pueden mapear también a pads o pedales: **PROGRAM 1–8** (cambiar de Program) y **PANIC**.

**OSC (tablet, TouchOSC, REAPER…):** en **SETTINGS → MIDI · OSC MAPPINGS**.
- Copia en tu controlador la IP y el puerto de **ESTE EQUIPO** (puerto 9000).
- Escribe la IP y el puerto del **DISPOSITIVO**.
- **FEEDBACK** devuelve los valores al controlador.
- **SOLO ESTA IP** ignora a cualquier otro equipo de la red.

---

## 12. Muestras, ROMs y cartuchos

| Qué | Formatos / dónde |
|---|---|
| Muestras de piano / instrumentos | `.npno`, `.nsmp`, `.nsmp3`, `.nsmp4`, `.ns2p`, `.ns3f`, `.nsp`, `.sf2`, `.sfz`, `.wav`. Se añaden en **LIBRARY → SAMPLE SOURCES** |
| ROMs del Roland MT-32 | Copia tus volcados (p. ej. `MT32_CONTROL.ROM` y `MT32_PCM.ROM`) en `%LOCALAPPDATA%\Live Stage\roms\mt32` y reinicia |
| Cartuchos DX7 (`.syx`) | BROWSER del synth → **FUENTES → DX7 · CARTUCHOS… → importar** |
| Bancos de patches D-50 (sysex) | Cópialos en `%LOCALAPPDATA%\Live Stage\roms` (también valen subcarpetas) |

Los bancos SF2 con varios presets piden en el buscador cuál cargar. Sin ROMs del MT-32, usa los presets
**LA32** del synth, que generan un parcial LA32 por síntesis.

---

## 13. Seguridad en directo

En **SETTINGS → SYSTEM**:
- **Programs con muestras que faltan:** Live Stage avisa al arrancar, para que no lo descubras en el escenario.
- **Recuperación tras un cierre inesperado:** si la app se cerró sin querer, al reabrirla ofrece recuperar el último estado. El autoguardado se hace cada 30 s.
- **Dispositivo de audio caído:** si se detiene, el motor lo reabre solo (*watchdog*).
- **RAM DE SAMPLES:** límite de memoria para muestras. Se liberan las que no se usan, nunca las de los 8 Programs ni las de las capas activas.
- **AUDIO:** carga del DSP, XRUN y bloques lentos. **RESET XRUN** pone los contadores a cero.
- **MIDI CLOCK · EXT CLOCK:** el tempo maestro sigue a un reloj externo.
- **PROGRAM CHANGE:** **OMNI**, un **CANAL** concreto o **IGNORAR**. Úsalo para que unos pads o una pedalera no cambien el Program por error.
- **ALL NOTES OFF · CC 120 / 123:** apágalo si algún controlador envía «all notes off» al conectarse. **PANIC** funciona siempre.

---

## 14. Acceso desde otros dispositivos

Por seguridad, solo la ventana de Live Stage puede controlar el motor. En **SETTINGS → SYSTEM → ACCESO WEB**:
- **NAVEGADOR** permite abrir la consola en un navegador de este equipo, en `http://localhost:8080`.
- **RED LOCAL** permite controlarla desde una tablet u otro equipo de la red. Requiere NAVEGADOR y reiniciar la app.

Actívalos solo en redes de confianza.

---

## 15. Live Stage como plug-in (VST3)

- El editor muestra **la misma consola**. Cada instancia tiene su propio motor.
- El **proyecto del DAW** guarda el sonido completo de la instancia: capas con sus muestras, synth, órgano, efectos, mapeos y nombres de preset.
- Los **8 Programs** aparecen como programas del DAW.
- **Automatización:** 27 parámetros, entre ellos:
  - niveles de módulos y de capas;
  - TIMBRE y macros del synth;
  - drawbars, DRIVE y ROTARY del órgano;
  - mezcla de reverb y delay.

  Al mover una perilla en la consola, el DAW puede grabarla.
- **Multitímbrico:** con **MIDI ROUTING** por canal, una sola pista puede tocar piano por el canal 1 y synth por el canal 2.
- El audio, la frecuencia de muestreo y las entradas MIDI los gestiona el DAW. SETTINGS lo indica.
- El plug-in no usa OSC.
- Las ROMs del MT-32 deben estar en `%LOCALAPPDATA%\Live Stage\roms\mt32`.

---

## 16. Opciones de arranque (avanzado)

`SonusLiveStage.exe [carpeta de muestras] [opciones]`

| Opción | Efecto |
|---|---|
| `D:\Samples` | Carpeta de muestras inicial |
| `--browser` | Abre la consola en el navegador en lugar de la ventana |
| `--web` | Permite el navegador solo en esta sesión |
| `--lan` | Escucha en la red local solo en esta sesión |
| `--console` | Muestra una consola con el log en vivo |
| `--headless` | Solo el motor y el servidor, sin ventana (pruebas) |

---

## 17. Solución de problemas

| Problema | Qué hacer |
|---|---|
| «No se pudo abrir el puerto 8080» | Live Stage ya está abierto: ciérralo o usa la ventana existente |
| No suena nada | SETTINGS → AUDIO ENGINE: comprueba el dispositivo. Mira que el módulo esté encendido (LED) y que el MASTER no esté a cero. Prueba PANIC |
| El teclado no responde | SETTINGS → MIDI INPUTS: actívalo. Si usas MIDI ROUTING, revisa que tenga una regla |
| Una capa no suena en una zona del teclado | ZONES: revisa LOW / HIGH de la capa y de su módulo |
| «Faltan muestras» en un Program | LIBRARY: vuelve a añadir o reubicar la carpeta de muestras |
| Chasquidos o cortes | Sube el buffer en AUDIO ENGINE y revisa DSP / XRUN en SETTINGS → SYSTEM |
| El plug-in muestra un aviso en lugar de la consola | Instala Microsoft Edge WebView2 Runtime. Mientras tanto, usa **ABRIR EN EL NAVEGADOR** |
| El Program cambia solo durante el show | SETTINGS → SYSTEM → PROGRAM CHANGE: elige un canal o IGNORAR |

Los logs están en `%LOCALAPPDATA%\Live Stage\logs`. Inclúyelos cuando informes de un problema.
