// Configuration: audio device, MIDI ports, MIDI CC / OSC mapping, system.
import { useEffect, useMemo, useState } from 'react';
import { api, type SystemSettings } from '../../api/client';
import { MidiRouting } from './MidiRouting';
import type { ControlState, HardwareStatus, MapScope } from '../../api/types';
import { notify, runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { THEMES, useTheme } from '../../store/theme';
import { setUiPref, useUiPrefs, UI_ZOOM_OPTIONS } from '../../store/uiPrefs';
import { applyUiZoom } from '../../store/uiZoom';
import { useEngine } from '../../store/hooks';
import { HttpAccess, OscDevice } from './NetworkSettings';
import { describeConflict, loadMappings, refLabel, setMappingState, sourceLabel, useMappings } from '../../store/mappings';
import { openMappingMenu } from '../../ui/MappingLayer';
import { Btn, Empty, Field, Panel, Row, Segmented, Select } from '../../ui/controls';

const BUFFERS = [64, 128, 256, 512, 1024];

function Audio({ hw, setHw }: { hw: HardwareStatus | null; setHw: (h: HardwareStatus) => void }) {
  const [apiName, setApiName] = useState<string | null>(null);
  const [uid, setUid] = useState<number | null>(null);
  const [rate, setRate] = useState<number | null>(null);
  const [buffer, setBuffer] = useState<number | null>(null);
  if (!hw) return <Panel title="AUDIO"><Empty>Leyendo el hardware…</Empty></Panel>;
  const selApi = apiName ?? hw.activeApi;
  const devices = hw.audioDevices.filter(d => d.api === selApi);
  const selUid = uid ?? (devices.some(d => d.uid === hw.activeAudioUid) ? hw.activeAudioUid : devices[0]?.uid);
  const device = devices.find(d => d.uid === selUid);
  const rates = device?.sampleRates.filter(r => r >= 44100 && r <= 96000) ?? [];
  const selRate = rate ?? (rates.includes(hw.sampleRate) ? hw.sampleRate : device?.preferredSr ?? 48000);
  const selBuffer = buffer ?? hw.bufferSize;
  const latency = (selBuffer / selRate) * 1000;
  const apply = async () => {
    if (selUid === undefined) return;
    const r = await runAction(() => api.configureAudio(selUid, selRate, selBuffer), 'Audio reconfigurado');
    if (r) { setHw(r); setApiName(null); setUid(null); setRate(null); setBuffer(null); }
  };
  return (
    <Panel title="AUDIO ENGINE" className="cfg-audio">
      <div className="kv">
        <span>ACTIVO</span><b>{hw.activeApi} · {hw.activeAudioDeviceName} · {hw.sampleRate} Hz · {hw.bufferSize} smp · {hw.latencyMs.toFixed(2)} ms</b>
        <span>DIAGNÓSTICO</span><b className={hw.audioUnderflows > 0 ? 'is-warn' : ''}>underruns {hw.audioUnderflows} · peor callback {(hw.maxAudioRenderMicros / 1000).toFixed(2)} ms{hw.asioCompiled ? ' · ASIO compilado' : ' · sin ASIO'}</b>
        {hw.lastAudioError && <><span>ERROR</span><b className="is-warn">{hw.lastAudioError}</b></>}
      </div>
      <div className="form-grid">
        <Field label="Driver"><Select label="Driver" value={selApi} options={hw.availableApis.map(a => ({ value: a, label: a }))} onChange={v => { setApiName(v); setUid(null); setRate(null); }} /></Field>
        <Field label="Dispositivo"><Select label="Dispositivo" value={selUid} options={devices.map(d => ({ value: d.uid, label: `${d.name} (${d.channels} ch)` }))} onChange={v => { setUid(Number(v)); setRate(null); }} /></Field>
        <Field label="Sample rate"><Select label="Sample rate" value={selRate} options={rates.map(r => ({ value: r, label: `${r} Hz` }))} onChange={v => setRate(Number(v))} /></Field>
        <Field label="Buffer"><Select label="Buffer" value={selBuffer} options={BUFFERS.map(b => ({ value: b, label: `${b} samples` }))} onChange={v => setBuffer(Number(v))} /></Field>
      </div>
      <Row>
        <span className="hint">Latencia estimada {latency.toFixed(2)} ms{selRate !== 48000 && rates.includes(48000)
          ? ' · Para directo se recomiendan 48 kHz: muchas interfaces USB son más estables y la latencia baja con el mismo buffer.' : ''}</span>
        <span className="grow" />
        <Btn onClick={() => runAction(() => api.testTone(), 'Tono de prueba 440 Hz')}>TONO DE PRUEBA</Btn>
        <Btn onClick={async () => { const r = await runAction(() => api.rescanHardware(), 'Hardware re-escaneado'); if (r) setHw(r); }}>RE-ESCANEAR</Btn>
        <Btn variant="primary" onClick={apply}>APLICAR</Btn>
      </Row>
    </Panel>
  );
}

function Midi({ hw, setHw }: { hw: HardwareStatus | null; setHw: (h: HardwareStatus) => void }) {
  const toggle = async (name: string, enabled: boolean) => { const r = await runAction(() => api.setMidiPort(name, enabled)); if (r) setHw(r); };
  return (
    <Panel title="MIDI INPUTS" className="cfg-midi" actions={<Btn small onClick={async () => { const r = await runAction(() => api.refreshMidi(), 'Puertos MIDI actualizados'); if (r) setHw(r); }}>REFRESCAR</Btn>}>
      <ul className="list">
        {hw?.midiPorts.map(p => (
          <li key={p.name} className="list__row">
            <button type="button" className={`led-switch${p.enabled ? ' is-on' : ''}`} aria-pressed={p.enabled} aria-label={`Activar ${p.name}`} onClick={() => toggle(p.name, !p.enabled)} />
            <span className="list__main list__main--static">{p.name}</span>
            <span className={p.connected ? 'tag tag--ok' : 'tag'}>{p.connected ? 'CONECTADO' : 'DESCONECTADO'}</span>
          </li>
        ))}
        {hw && hw.midiPorts.length === 0 && <Empty>No se detectan puertos MIDI.</Empty>}
      </ul>
    </Panel>
  );
}

const SCOPE_FILTERS = ['TODOS', 'SISTEMA', 'SONIDOS', 'PRESETS', 'PROGRAMA'] as const;
const FILTER_SCOPE: Record<number, MapScope | null> = { 0: null, 1: 'system', 2: 'sound', 3: 'module', 4: 'program' };

function Mapping() {
  const { state } = useMappings();
  const [control, setControl] = useState<ControlState | null>(null);
  const [filter, setFilter] = useState(0);
  const [query, setQuery] = useState('');
  const [oscPort, setOscPort] = useState('');
  useEffect(() => { api.control().then(setControl).catch(() => {}); }, []);
  // Show the last MIDI / OSC message received (useful to identify a controller).
  useEffect(() => { const t = setInterval(() => void loadMappings(), 1500); return () => clearInterval(t); }, []);
  const matches = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!control || q.length < 2) return [];
    return control.parameters.filter(p => p.id.toLowerCase().includes(q)).slice(0, 40);
  }, [control, query]);
  const rows = (state?.assignments ?? []).filter(a => FILTER_SCOPE[filter] === null || a.scope === FILTER_SCOPE[filter]);
  const remove = async (paramId: string, scope: MapScope) => {
    try { setMappingState(await api.mappingOp({ op: 'remove', paramId, scope })); notify('Mapeo eliminado', 'ok'); }
    catch (e) { notify((e as Error).message, 'error'); }
  };
  const setPort = async () => {
    const port = Number(oscPort);
    try { setMappingState(await api.mappingOp({ op: 'oscPort', port })); notify(`OSC en el puerto ${port}`, 'ok'); setOscPort(''); }
    catch (e) { notify((e as Error).message, 'error'); }
  };
  const last = state?.lastInput as { type?: string; kind?: string; channel?: number; number?: number; value?: number; address?: string } | null | undefined;
  const lastText = !last ? '—' : last.type === 'osc' ? `OSC ${last.address} = ${Number(last.value).toFixed(3)}`
    : `${last.kind === 'note' ? 'NOTE' : 'CC'} ${last.number} · canal ${last.channel} = ${last.value}`;
  return (
    <Panel title="MIDI · OSC MAPPINGS" className="cfg-mapping">
      <p className="hint">Clic derecho (o mantener pulsado) sobre cualquier knob, fader o botón para asignarlo. <b>MAP</b> en la cabecera resalta los controles mapeados.
        Prioridad: PROGRAMA &gt; PERFORMANCE / PRESET &gt; SONIDO &gt; SISTEMA. Los mapeos de SISTEMA son permanentes.</p>
      {(state?.conflicts.length ?? 0) > 0 && (
        <div className="mapping-conflicts" role="alert">
          <b>⚠ Mensajes también mapeados en SISTEMA</b>
          <ul>{state!.conflicts.map(c => <li key={c.source}>{describeConflict(c)}</li>)}</ul>
        </div>
      )}
      <div className="mapping">
        <div className="mapping__lists">
          <Segmented label="Ámbito" options={SCOPE_FILTERS} value={filter} onChange={setFilter} />
          <ul className="list list--scroll list--dense mapping-list">
            {rows.map(a => (
              <li key={`${a.scope}-${a.module ?? ''}-${a.paramId}`} className={`list__row${a.effective ? '' : ' is-shadowed'}`}>
                <button type="button" className="list__main" title="Editar mapeo"
                  onClick={e => openMappingMenu({ paramId: a.paramId, label: a.paramId, x: e.clientX, y: e.clientY })}>
                  <span><span className={`tag tag--scope-${a.scope}`}>{refLabel(a)}</span>{a.paramId}</span>
                  <span className="muted">{sourceLabel(a)}{a.effective ? '' : ' · OCULTO'}</span>
                </button>
                <button type="button" className="icon-btn" aria-label={`Quitar mapeo de ${a.paramId}`} onClick={() => void remove(a.paramId, a.scope)}>×</button>
              </li>
            ))}
            {rows.length === 0 && <Empty>Sin mapeos en este ámbito.</Empty>}
          </ul>
        </div>
        <div className="mapping__picker">
          <Field label="Mapear un parámetro sin control en pantalla">
            <input className="input" type="search" placeholder="Buscar ID (p. ej. programSlot, masterBpm, L1_)…" value={query} onChange={e => setQuery(e.target.value)} />
          </Field>
          <ul className="list list--scroll list--dense">
            {matches.map(p => (
              <li key={p.id}><button type="button" className="list__main" onClick={e => openMappingMenu({ paramId: p.id, label: p.id, x: e.clientX, y: e.clientY })}>
                <span>{p.id}</span><span className="muted">{p.min} … {p.max}</span></button></li>
            ))}
          </ul>
          {query.trim().length < 2 && <Empty>Escribe al menos dos letras.</Empty>}
          <p className="hint">Último mensaje recibido: <b>{lastText}</b></p>
          <h3 className="subhead">OSC · puerto {state?.osc.port} {state?.osc.running ? '· activo' : '· detenido'}</h3>
          <form className="row" onSubmit={e => { e.preventDefault(); if (oscPort) void setPort(); }}>
            <input className="input" inputMode="numeric" placeholder={String(state?.osc.port ?? 9000)} value={oscPort} onChange={e => setOscPort(e.target.value)} aria-label="Puerto OSC" />
            <Btn type="submit" disabled={!oscPort}>CAMBIAR PUERTO</Btn>
          </form>
          <OscDevice />
        </div>
      </div>
    </Panel>
  );
}

/** Header monitor (MIDI · voices · pedal · LIVE): a per-browser display preference. */
function StatusBarChoice() {
  const { statusBar } = useUiPrefs();
  return (
    <Row>
      <Btn small led active={statusBar} aria-pressed={statusBar} onClick={() => setUiPref('statusBar', !statusBar)}
        title="Muestra u oculta en la barra superior el monitor de entrada MIDI, voces, pedal y LIVE">MONITOR MIDI</Btn>
      <span className="hint">Barra superior: entrada MIDI, voces, pedal y LIVE{statusBar ? '' : ' (oculta; los cortes de audio, la carga DSP alta y la reconexión de audio siguen avisando)'}.</span>
    </Row>
  );
}

/** Preferencia local de visualizacion: no modifica presets ni el estado DSP. */
function InterfaceZoomChoice() {
  const { uiZoom } = useUiPrefs();
  const change = (value: number) => {
    setUiPref('uiZoom', value);
    applyUiZoom(value);
  };
  return (
    <div className="cfg-zoom">
      <Row wrap>
        <span className="live-safety__label">ZOOM UI</span>
        <Segmented wrap label="Escala de la interfaz" options={UI_ZOOM_OPTIONS.map(v => `${v}%`)}
          values={[...UI_ZOOM_OPTIONS]} value={uiZoom} onChange={v => change(Number(v))} />
        <Btn small onClick={() => change(100)} disabled={uiZoom === 100}>RESTABLECER</Btn>
      </Row>
      <p className="hint">Escala de toda la consola. Se guarda únicamente en este equipo y no modifica el diseño, los sonidos ni los programas. En pantallas pequeñas prueba 75–90 %.</p>
    </div>
  );
}

/** v32.4 live safety: sample-RAM budget, audio telemetry and MIDI clock follow. */
function LiveSafety() {
  const [sys, setSys] = useState<SystemSettings | null>(null);
  useEffect(() => { api.system().then(setSys).catch(() => setSys(null)); }, []);
  const underflows = useEngine(s => s?.audioUnderflows ?? 0);
  const over = useEngine(s => s?.audioOverBudgetCallbacks ?? 0);
  const peak = useEngine(s => s?.dspPeak ?? 0);
  const maxRender = useEngine(s => s?.maxRenderMicros ?? 0);
  const recoveries = useEngine(s => s?.audioRecoveries ?? 0);
  const clock = useEngine(s => s?.midiClockSync ?? false);
  const budgets = [2048, 4096, 8192, 16384];
  const setMidi = async (patch: { midiProgramChannel?: number; midiAllNotesOff?: boolean }, label: string) => {
    const r = await runAction(() => api.setSystem(patch), label);
    if (r) setSys(r);
  };
  const setBudget = async (mb: number) => {
    const r = await runAction(() => api.setSampleMemoryBudget(mb), `RAM de samples: ${mb / 1024} GB`);
    if (r) setSys(r);
  };
  return (
    <div className="live-safety">
      <Row wrap>
        <span className="live-safety__label">RAM DE SAMPLES</span>
        <Segmented label="Presupuesto de RAM de samples" options={budgets.map(b => `${b / 1024} GB`)} values={budgets}
          value={sys?.sampleMemoryBudgetMB ?? 4096} onChange={v => void setBudget(Number(v))} />
        <span className="hint">{sys ? `${Math.round(sys.sampleResidentMB)} MB en uso.` : ''} Por encima del presupuesto se liberan los
          instrumentos sin usar (nunca los de los 8 programas ni las capas activas).</span>
      </Row>
      <Row wrap>
        <span className="live-safety__label">AUDIO</span>
        <span className="hint">DSP pico <b>{Math.round(peak * 100)} %</b> · render máx. <b>{maxRender} µs</b> · XRUN <b>{underflows}</b> ·
          bloques lentos <b>{over}</b> · recuperaciones <b>{recoveries}</b></span>
        <Btn small onClick={() => void runAction(() => api.resetAudioCounters(), 'Contadores de audio reiniciados')}>RESET XRUN</Btn>
      </Row>
      <Row wrap>
        <span className="live-safety__label">MIDI CLOCK</span>
        <Btn small led active={clock} onClick={() => void runAction(() => api.setParam('midiClockSync', clock ? 0 : 1), clock ? 'Tempo interno' : 'Tempo: MIDI CLOCK externo')}>EXT CLOCK</Btn>
        <span className="hint">El tempo master sigue al reloj externo y START reinicia los secuenciadores.</span>
      </Row>
      <Row wrap>
        <span className="live-safety__label">PROGRAM CHANGE</span>
        <select className="input" aria-label="Canal de Program Change" value={sys?.midiProgramChannel ?? 0}
          onChange={e => void setMidi({ midiProgramChannel: Number(e.target.value) }, 'Program Change actualizado')}>
          <option value={-1}>IGNORAR</option>
          <option value={0}>OMNI (todos los canales)</option>
          {Array.from({ length: 16 }, (_, i) => <option key={i} value={i + 1}>CANAL {i + 1}</option>)}
        </select>
        <span className="hint">Program Change n recupera el programa n mod 8. Elige un canal para que otros controladores (pads, pedaleras) no cambien el programa en directo.</span>
      </Row>
      <Row wrap>
        <span className="live-safety__label">ALL NOTES OFF</span>
        <Btn small led active={sys?.midiAllNotesOff ?? true}
          onClick={() => void setMidi({ midiAllNotesOff: !(sys?.midiAllNotesOff ?? true) }, (sys?.midiAllNotesOff ?? true) ? 'CC 120/123 ignorados' : 'CC 120/123 aceptados')}>
          CC 120 / 123</Btn>
        <span className="hint">Encendido: los mensajes MIDI «all notes off» cortan el sonido (estándar). Apágalo si un controlador los envía al conectarse. PANIC siempre funciona.</span>
      </Row>
    </div>
  );
}

function ThemeChoice() {
  const [theme, setTheme] = useTheme();
  return <Segmented label="Tema visual" options={THEMES.map(t => t.label)} value={THEMES.findIndex(t => t.id === theme)}
    onChange={i => setTheme(THEMES[i].id)} />;
}

export default function SettingsView() {
  const [hw, setHw] = useState<HardwareStatus | null>(null);
  useEffect(() => { api.hardware().then(setHw).catch(e => notify((e as Error).message, 'error')); }, []);
  return (
    <div className="view view--settings">
      <div className="stack">
        {hw?.host === 'plugin' ? (
          <Panel title="AUDIO · MIDI" className="cfg-audio">
            <p className="hint">Live Stage funciona como plug-in: el DAW gestiona la salida de audio, la frecuencia de muestreo, el buffer
              y las entradas MIDI (la pista donde está insertado). Los Programs, presets, mapeos y la biblioteca son los mismos que en la app.</p>
          </Panel>
        ) : (
          <>
            <Audio hw={hw} setHw={setHw} />
            <Midi hw={hw} setHw={setHw} />
          </>
        )}
        <MidiRouting />
        <Panel title="SYSTEM" className="cfg-system" actions={<ThemeChoice />}>
          <div className="knobs"><ParamKnob id="maxVoices" label="MAX VOICES" /><ParamKnob id="tune" label="MASTER TUNE" /></div>
          <StatusBarChoice />
          <InterfaceZoomChoice />
          <LiveSafety />
          <HttpAccess />
          <p className="hint">Interfaz única React · sincronización de parámetros en tiempo real con el motor C++.</p>
        </Panel>
      </div>
      <Mapping />
    </div>
  );
}
