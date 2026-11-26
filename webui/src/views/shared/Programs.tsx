// Global program display (OLED) and the 8 program pads (2 rows × 4).
import { useCallback, useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { ProgramSlot } from '../../api/types';
import { ORGAN_MODELS, PIANO_LAYER_COUNT, PROGRAM_SLOTS, SYNTH_LAYER_COUNT } from '../../data/constants';
import { SYNTH_FACTORY } from '../../data/synthFactory';
import { displayName, useFactoryMeta } from '../../store/synthFactory';
import { useOrganPresetRef, useSynthPresetRef } from '../../store/synthUserPatch';
import { useEngine } from '../../store/hooks';
import { runAction } from '../../store/notify';
import { Btn, Lcd, Panel, Row } from '../../ui/controls';
import { Modal } from '../../ui/Modal';
import { PixelText } from '../../ui/PixelText';
import { cleanName } from './PianoLayers';
import { mapAttrsFor, useMappings } from '../../store/mappings';

// Engine icons on the OLED (keyboard, waveform, organ), as on the earlier front panel.
const EngineIcon = ({ kind }: { kind: 'piano' | 'synth' | 'organ' }) => (
  <svg className="oled-engine__icon" viewBox="0 0 24 24" aria-hidden="true">
    {kind === 'piano' && <><rect x="2" y="5" width="20" height="14" rx="2" /><path d="M7 5v8m5-8v8m5-8v8M6 13h12M8 5v8h3V5m4 0v8h3V5" /></>}
    {kind === 'synth' && <path d="M2 12h3l3-7 4 14 4-11 3 4h3" />}
    {kind === 'organ' && <><rect x="2" y="4" width="20" height="16" rx="2" /><path d="M5 8h14M5 13h14M8 8v5m5-5v5m4 0v4" /></>}
  </svg>
);

export function ProgramDisplay() {
  const slot = useEngine(s => s?.activeProgramSlot ?? 0);
  const name = useEngine(s => s?.programName ?? '');
  const voices = useEngine(s => s?.activeVoices ?? 0);
  const pianoOn = useEngine(s => Boolean(s?.params?.pianoEnabled));
  const synthOn = useEngine(s => Boolean(s?.params?.synthEnabled));
  const organOn = useEngine(s => Boolean(s?.params?.organEnabled));
  // Piano layers playing: enabled and holding a sample.
  const pianoLayers = useEngine(s => (s?.pianoLayers ?? []).map((l, i) => (l?.sourcePath && s?.params?.pianoLayers?.[i]?.enabled ? cleanName(l.name) : '')).filter(Boolean).join(' · '));
  const pianoCount = useEngine(s => (s?.pianoLayers ?? []).filter((l, i) => l?.sourcePath && s?.params?.pianoLayers?.[i]?.enabled).length);
  const synthCount = useEngine(s => (s?.params?.synthLayers ?? []).filter(l => l?.enabled).length);
  const synthId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const organModel = Math.round(useEngine(s => s?.params?.organModel ?? 0));
  const { overrides } = useFactoryMeta();
  const synthRef = useSynthPresetRef();
  const organRef = useOrganPresetRef();
  const engines = [
    pianoOn && { kind: 'piano' as const, type: 'PIANO', text: pianoLayers || 'SIN MUESTRA' },
    synthOn && { kind: 'synth' as const, type: 'SYNTH', text: synthRef?.name ?? displayName(SYNTH_FACTORY[synthId - 1], overrides) },
    organOn && { kind: 'organ' as const, type: 'ORGAN', text: organRef?.name ?? ORGAN_MODELS[organModel] ?? 'ORGAN' },
  ].filter(Boolean) as { kind: 'piano' | 'synth' | 'organ'; type: string; text: string }[];
  return (
    <Lcd className="program-lcd">
      <div className="lcd__bar"><span>PROGRAM {String(slot + 1).padStart(2, '0')}</span><span className="program-lcd__status">LIVE STAGE</span></div>
      <div className="program-lcd__name"><PixelText text={name || '—'} /></div>
      <div className="lcd__sep" />
      <div className="program-lcd__engines">
        {engines.map(e => (
          <div key={e.kind} className={`oled-engine oled-engine--${e.kind}`}>
            <EngineIcon kind={e.kind} /><span className="oled-engine__type">{e.type}</span><strong><PixelText text={e.text} /></strong>
          </div>
        ))}
        {engines.length === 0 && <div className="oled-engine oled-engine--none">SIN MÓDULOS ACTIVOS</div>}
      </div>
      <div className="lcd__foot">
        <span>PIANO <b>{pianoCount}/{PIANO_LAYER_COUNT}</b></span>
        <span>SYNTH <b>{synthCount}/{SYNTH_LAYER_COUNT}</b></span>
        <span>VOICES <b>{voices}</b></span>
      </div>
    </Lcd>
  );
}

export function ProgramPads() {
  // Pads are mappable (programSlot01-08): a MIDI pad or footswitch recalls the program.
  const { effective } = useMappings();
  const active = useEngine(s => s?.activeProgramSlot ?? 0);
  const currentName = useEngine(s => s?.programName ?? '');
  const [programs, setPrograms] = useState<ProgramSlot[]>([]);
  const [storeMode, setStoreMode] = useState(false);
  const [storeSlot, setStoreSlot] = useState<number | null>(null);
  const [storeName, setStoreName] = useState('');
  const [pendingSlot, setPendingSlot] = useState<number | null>(null);
  const reload = useCallback(() => { api.programs().then(setPrograms).catch(() => {}); }, []);
  useEffect(reload, [reload]);

  const press = async (slot: number) => {
    if (storeMode) { setStoreSlot(slot); setStoreName(currentName); return; }
    if (pendingSlot !== null) return;
    setPendingSlot(slot);
    try { await runAction(() => api.recallProgram(slot), `Program ${slot + 1}`, true); }
    finally { setPendingSlot(null); }
  };
  const store = async () => {
    if (storeSlot === null) return;
    const result = await runAction(() => api.storeProgram(storeSlot, storeName.trim() || undefined), `Guardado en Program ${storeSlot + 1}`);
    // Keep the confirmation dialog open when the engine rejects or times out.
    if (result) {
      setPrograms(result.programs);
      setStoreSlot(null);
      setStoreMode(false);
    }
  };
  const init = () => runAction(() => api.initProgram(), 'Edit buffer inicializado (no se ha guardado)', true);

  return (
    <Panel title="PROGRAM MEMORY · 8 SLOTS" variant="chassis" className="programs" actions={<>
      <Btn small onClick={init} title="Reinicia el buffer de edición; no sobrescribe ningún programa">INIT</Btn>
      <Btn small variant="primary" active={storeMode} onClick={() => setStoreMode(v => !v)}
        title="Activa STORE y pulsa el pad de destino">{storeMode ? 'ELIGE PAD…' : 'STORE'}</Btn>
    </>}>
      <div className="programs__meta"><span>PROGRAM BANK <b>01 — 08</b></span><span>{storeMode ? 'SELECT DESTINATION' : 'RECALL / STORE'}</span></div>
      <div className={`pads${storeMode ? ' is-storing' : ''}`} role="group" aria-label="Ocho programas en dos filas de cuatro">
        {Array.from({ length: PROGRAM_SLOTS }, (_, i) => (
          <div key={i} className="pad-cell">
            <button type="button" className={`pad${i === active ? ' is-active' : ''}${pendingSlot === i ? ' is-loading' : ''}`}
              onClick={() => void press(i)} disabled={pendingSlot !== null}
              title={`${storeMode ? 'Guardar en' : 'Recuperar'} programa ${i + 1}: ${programs[i]?.name ?? `Program ${i + 1}`}`}
              aria-label={`Programa ${i + 1}: ${programs[i]?.name ?? `Program ${i + 1}`}`}
              aria-pressed={i === active && !storeMode} {...mapAttrsFor(effective, `programSlot${String(i + 1).padStart(2, '0')}`)}>
              <span className="pad__number">{String(i + 1).padStart(2, '0')}</span>
              <span className="pad__caption" aria-hidden="true">PROGRAM</span>
              <span className="pad__oval" aria-hidden="true" />
              <span className={`led pad__led${i === active ? ' is-on' : ''}`} aria-hidden="true" />
            </button>
            <span className="pad__name" title={programs[i]?.name}>{programs[i]?.name ?? `Program ${i + 1}`}</span>
          </div>
        ))}
      </div>
      {storeSlot !== null && (
        <Modal title={`GUARDAR EN PROGRAM ${storeSlot + 1}`} onClose={() => setStoreSlot(null)}>
          <form className="form" onSubmit={e => { e.preventDefault(); void store(); }}>
            <label className="field"><span className="field__label">Nombre</span>
              <input className="input" maxLength={48} value={storeName} onChange={e => setStoreName(e.target.value)} autoFocus /></label>
            <p className="hint">Se sobrescribe «{programs[storeSlot]?.name ?? `Program ${storeSlot + 1}`}».</p>
            <Row><Btn onClick={() => setStoreSlot(null)}>CANCELAR</Btn><Btn variant="primary" type="submit">GUARDAR</Btn></Row>
          </form>
        </Modal>
      )}
    </Panel>
  );
}
