// Configuration: audio device, MIDI ports, MIDI CC / OSC mapping, system.
import { useEffect, useMemo, useState } from 'react';
import { api } from '../../api/client';
import type { ControlState, HardwareStatus, MapScope } from '../../api/types';
import { notify, runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { THEMES, useTheme } from '../../store/theme';
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
        <span className="hint">Latencia estimada {latency.toFixed(2)} ms</span>
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
        </div>
      </div>
    </Panel>
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
        <Audio hw={hw} setHw={setHw} />
        <Midi hw={hw} setHw={setHw} />
        <Panel title="SYSTEM" className="cfg-system" actions={<ThemeChoice />}>
          <div className="knobs"><ParamKnob id="maxVoices" label="MAX VOICES" /><ParamKnob id="tune" label="MASTER TUNE" /></div>
          <p className="hint">Interfaz única React · sincronización de parámetros en tiempo real con el motor C++.</p>
        </Panel>
      </div>
      <Mapping />
    </div>
  );
}
