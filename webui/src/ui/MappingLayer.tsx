// Right-click (or the context-menu key, or a long press) on any control with
// data-param opens its MIDI / OSC mapping menu: scope, LEARN, OSC address, pickup,
// button mode, range, remove. One instance lives in the shell (event delegation).
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { api } from '../api/client';
import type { Assignment, ButtonMode, LearnCapture, MapScope, MidiSource, Takeover } from '../api/types';
import { engine } from '../store/engine';
import { useEngine } from '../store/hooks';
import {
  MODULE_LABEL, SCOPE_LABEL, loadMappings, midiLabel, modulePresetLabel, refLabel, setMappingState, sharedWith, sourceLabel,
  soundLayerOf, syncMappingRevision, useMappings,
} from '../store/mappings';
import { notify } from '../store/notify';

export interface MappingTarget { paramId: string; label: string; x: number; y: number }
type Target = MappingTarget;
let openExternally: ((t: Target) => void) | null = null;
/** Opens the mapping menu for a parameter without an on-screen control (SETTINGS search). */
export function openMappingMenu(target: MappingTarget) { openExternally?.(target); }

const TAKEOVERS: { id: Takeover; label: string; title: string }[] = [
  { id: 'pickup', label: 'PICKUP', title: 'El control físico no actúa hasta alcanzar el valor actual' },
  { id: 'jump', label: 'SALTO', title: 'El valor salta a la posición del control físico' },
  { id: 'relative', label: 'RELATIVO', title: 'Encoder sin fin (incrementos)' },
];
const BUTTONS: { id: ButtonMode; label: string }[] = [
  { id: 'continuous', label: 'CONTINUO' }, { id: 'toggle', label: 'CONMUTAR' }, { id: 'momentary', label: 'MOMENTÁNEO' },
];

function controlLabel(el: Element, id: string): string {
  const text = el.querySelector('.knob__label, .fader__label, .field__label, .led-choice__label')?.textContent
    ?? el.getAttribute('aria-label') ?? el.getAttribute('title') ?? el.textContent ?? '';
  return text.trim().slice(0, 40) || id;
}

export function MappingLayer() {
  const [target, setTarget] = useState<Target | null>(null);
  const revision = useEngine(s => s?.mappingRev);
  useEffect(() => { void loadMappings(); openExternally = setTarget; return () => { openExternally = null; }; }, []);
  useEffect(() => { syncMappingRevision(revision); }, [revision]);

  useEffect(() => {
    const openAt = (el: Element, x: number, y: number) => {
      const paramId = el.getAttribute('data-param');
      if (!paramId) return;
      if (x === 0 && y === 0) { const r = el.getBoundingClientRect(); x = r.left + r.width / 2; y = r.bottom; }
      setTarget({ paramId, label: controlLabel(el, paramId), x, y });
    };
    const onContext = (e: MouseEvent) => {
      const el = (e.target as Element | null)?.closest?.('[data-param]');
      if (!el || (e.target as Element).closest('.mapping-menu')) return;
      e.preventDefault();
      openAt(el, e.clientX, e.clientY);
    };
    // Touch screens: a long press opens the same menu.
    let timer = 0, startX = 0, startY = 0;
    const onDown = (e: PointerEvent) => {
      if (e.pointerType !== 'touch') return;
      const el = (e.target as Element | null)?.closest?.('[data-param]');
      if (!el) return;
      startX = e.clientX; startY = e.clientY;
      timer = window.setTimeout(() => openAt(el, startX, startY), 650);
    };
    const cancel = (e: PointerEvent) => {
      if (e.type === 'pointermove' && Math.hypot(e.clientX - startX, e.clientY - startY) < 10) return;
      window.clearTimeout(timer);
    };
    document.addEventListener('contextmenu', onContext);
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('pointerup', cancel);
    document.addEventListener('pointermove', cancel);
    return () => {
      document.removeEventListener('contextmenu', onContext);
      document.removeEventListener('pointerdown', onDown);
      document.removeEventListener('pointerup', cancel);
      document.removeEventListener('pointermove', cancel);
    };
  }, []);
  return target ? createPortal(<MappingMenu target={target} onClose={() => setTarget(null)} />, document.body) : null;
}

type Pending = { source: { midi: MidiSource | null; osc: string | null }; label: string; conflicts: Assignment[] };

function MappingMenu({ target, onClose }: { target: Target; onClose: () => void }) {
  const { paramId } = target;
  const { byParam, effective } = useMappings();
  const owner = engine.moduleOf(paramId);
  const all = byParam.get(paramId) ?? [];
  const inEffect = effective.get(paramId);
  const soundLayer = soundLayerOf(paramId);
  const [scope, setScope] = useState<MapScope>(inEffect?.scope ?? (soundLayer >= 0 ? 'sound' : owner >= 0 ? 'module' : 'system'));
  const current = all.find(a => a.scope === scope);
  const [learning, setLearning] = useState(false);
  const [allowReserved, setAllowReserved] = useState(false);
  // v33 (M4): MIDI keyboards (ports) a mapping can be restricted to.
  const [ports, setPorts] = useState<string[]>([]);
  useEffect(() => {
    api.midiRouting().then(r => setPorts(r.sources.filter(s => s.id > 0).map(s => s.name))).catch(() => setPorts([]));
  }, []);
  const [pending, setPending] = useState<Pending | null>(null);
  const [osc, setOsc] = useState(current?.osc ?? '');
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ left: target.x, top: target.y });
  useEffect(() => { setOsc(current?.osc ?? ''); }, [current?.osc, scope]);

  // Keep the menu inside the viewport.
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setPos({
      left: Math.max(8, Math.min(target.x, window.innerWidth - r.width - 8)),
      top: Math.max(8, Math.min(target.y, window.innerHeight - r.height - 8)),
    });
  }, [target.x, target.y, scope, learning, pending, all.length]);

  const op = useCallback(async (body: Record<string, unknown>, success?: string) => {
    try {
      const state = await api.mappingOp(body);
      setMappingState(state);
      if (success) notify(success, 'ok');
      return true;
    } catch (e) { notify((e as Error).message, 'error'); return false; }
  }, []);
  const assign = (fields: Record<string, unknown>, success?: string) => op({ op: 'assign', paramId, scope, ...fields }, success);
  const remove = (what: Record<string, boolean>, success: string) => op({ op: 'remove', paramId, scope, ...what }, success);

  const commit = async (source: Pending['source'], label: string) => {
    setPending(null);
    const fields: Record<string, unknown> = {};
    if (source.midi) fields.midi = source.midi;
    if (source.osc) fields.osc = source.osc;
    await assign(fields, `${label} → ${target.label} (${SCOPE_LABEL[scope]})`);
  };

  // A captured message: SYSTEM conflicts must be confirmed before assigning.
  const captured = (c: LearnCapture) => {
    const source = c.type === 'osc'
      ? { midi: null, osc: c.address ?? '' }
      // CCs from a single controller default to omni; notes keep their channel (pads).
      : { midi: { type: c.kind ?? 'cc', channel: c.kind === 'note' ? c.channel ?? 0 : current?.midi?.channel ?? 0, number: c.number ?? 0 } as MidiSource, osc: null };
    const label = source.midi ? midiLabel(source.midi) : `OSC ${source.osc}`;
    const shared = sharedWith(source, paramId);
    const conflicts = shared.filter(a => (scope === 'system') !== (a.scope === 'system'));
    if (conflicts.length) setPending({ source, label, conflicts });
    else void commit(source, label);
  };

  const startLearn = async () => {
    if (!(await op({ op: 'learn', allowReserved }))) return;
    setLearning(true);
  };
  const stopLearn = useCallback(async () => {
    setLearning(false);
    try { await api.mappingOp({ op: 'cancel' }); } catch { /* engine gone */ }
  }, []);
  useEffect(() => {
    if (!learning) return;
    let alive = true;
    const startedAt = Date.now();
    const poll = async () => {
      while (alive) {
        await new Promise(r => setTimeout(r, 120));
        try {
          const { learn } = await api.mappingsLearn();
          if (!learn.active) {
            if (alive && learn.captured) { setLearning(false); captured(learn.captured); }
            else if (alive) setLearning(false);
            return;
          }
        } catch { /* keep waiting */ }
        if (Date.now() - startedAt > 20000) { if (alive) { notify('MIDI LEARN: tiempo agotado', 'info'); void stopLearn(); } return; }
      }
    };
    void poll();
    return () => { alive = false; };
  }, [learning]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return;
      e.stopPropagation();
      if (learning) void stopLearn(); else if (pending) setPending(null); else onClose();
    };
    const onDown = (e: PointerEvent) => { if (!ref.current?.contains(e.target as Node)) { if (learning) void stopLearn(); onClose(); } };
    window.addEventListener('keydown', onKey, true);
    document.addEventListener('pointerdown', onDown, true);
    return () => { window.removeEventListener('keydown', onKey, true); document.removeEventListener('pointerdown', onDown, true); };
  }, [learning, pending, onClose, stopLearn]);
  useEffect(() => () => { if (learning) void api.mappingOp({ op: 'cancel' }).catch(() => {}); }, [learning]);

  const spec = engine.spec(paramId);
  const alsoDriven = current ? sharedWith({ midi: current.midi, osc: current.osc }, paramId) : [];
  const scopes: MapScope[] = soundLayer >= 0 ? ['sound', 'module', 'program', 'system'] : ['module', 'program', 'system'];
  return (
    <div ref={ref} className="mapping-menu" role="dialog" aria-label={`Mapeo de ${target.label}`} style={{ left: pos.left, top: pos.top }}>
      <header className="mapping-menu__head">
        <strong>{target.label}</strong>
        <span className="mapping-menu__id">{paramId}{owner >= 0 ? ` · ${MODULE_LABEL[owner]}` : ' · GLOBAL'}</span>
        <span className="mapping-menu__state">
          {inEffect ? <>EN USO: <b>{sourceLabel(inEffect)}</b> · {refLabel(inEffect)}</> : 'SIN MAPEO'}
        </span>
      </header>

      <div className="mapping-menu__scopes" role="radiogroup" aria-label="Ámbito del mapeo">
        {scopes.map(s => {
          const disabled = s === 'module' && owner < 0;
          const has = all.find(a => a.scope === s);
          return (
            <button key={s} type="button" role="radio" aria-checked={scope === s} disabled={disabled}
              className={`segmented__item${scope === s ? ' is-active' : ''}`} onClick={() => { setScope(s); setPending(null); }}
              title={disabled ? 'Parámetro global: no pertenece a ningún preset de módulo' : undefined}>
              {s === 'module' && owner >= 0 ? modulePresetLabel(owner) : s === 'sound' ? `SONIDO L${soundLayer + 1}` : SCOPE_LABEL[s]}
              {has && <span className={`mapping-menu__dot mapping-menu__dot--${s}${has.effective ? '' : ' is-shadowed'}`} aria-label={has.effective ? 'en uso' : 'oculto por un ámbito superior'} />}
            </button>
          );
        })}
      </div>

      {pending ? (
        <div className="mapping-menu__alert" role="alert">
          <p><b>{pending.label}</b> {scope === 'system' ? 'ya controla en un preset o programa' : 'ya controla en SISTEMA'}:</p>
          <ul>{pending.conflicts.map(a => <li key={a.paramId + a.scope}>{a.paramId} <span className="muted">({refLabel(a)})</span></li>)}</ul>
          <p className="muted">Moverlo cambiará ambos parámetros.</p>
          <div className="row">
            <button type="button" className="btn btn--sm" onClick={() => setPending(null)}>CANCELAR</button>
            <button type="button" className="btn btn--sm btn--primary" onClick={() => void commit(pending.source, pending.label)}>ASIGNAR IGUALMENTE</button>
          </div>
        </div>
      ) : learning ? (
        <div className="mapping-menu__learn" role="status">
          <span className="led is-on" aria-hidden="true" /> Mueve un control MIDI o envía un mensaje OSC… <span className="muted">(ESC cancela)</span>
          <button type="button" className="btn btn--sm" onClick={() => void stopLearn()}>CANCELAR</button>
        </div>
      ) : (
        <>
          <div className="mapping-menu__row">
            <span className="mapping-menu__key">MIDI</span>
            <span className="mapping-menu__value">{current?.midi ? midiLabel(current.midi) : '—'}</span>
            {current?.midi && current.midi.type === 'cc' && (
              <select className="select mapping-menu__channel" aria-label="Canal MIDI" value={current.midi.channel}
                onChange={e => void assign({ midi: { ...current.midi, channel: Number(e.target.value) } })}>
                {Array.from({ length: 17 }, (_, i) => <option key={i} value={i}>{i === 0 ? 'OMNI' : `CH ${i}`}</option>)}
              </select>
            )}
            {current?.midi && ports.length > 0 && (
              <select className="select mapping-menu__channel" aria-label="Teclado MIDI" value={current.midi.port ?? ''}
                title="Solo este teclado (útil con varios teclados conectados)"
                onChange={e => { const { port: _old, ...rest } = current.midi!; void assign({ midi: e.target.value ? { ...rest, port: e.target.value } : rest }); }}>
                <option value="">CUALQUIER TECLADO</option>
                {[...new Set([...ports, ...(current.midi.port ? [current.midi.port] : [])])].map(p => <option key={p} value={p}>{p}</option>)}
              </select>
            )}
            <button type="button" className="btn btn--sm btn--primary" onClick={() => void startLearn()}>APRENDER</button>
            {current?.midi && <button type="button" className="btn btn--sm" onClick={() => void remove({ midi: true }, 'MIDI quitado')}>QUITAR</button>}
          </div>
          <label className="mapping-menu__check">
            <input type="checkbox" checked={allowReserved} onChange={e => setAllowReserved(e.target.checked)} />
            Permitir rueda (CC 1), pedal (CC 64) y CC 120–127
          </label>
          <form className="mapping-menu__row" onSubmit={e => { e.preventDefault(); if (osc.trim()) void assign({ osc: osc.trim() }, `OSC ${osc.trim()} → ${target.label}`); }}>
            <span className="mapping-menu__key">OSC</span>
            <input className="input grow" value={osc} placeholder="/live/param" aria-label="Dirección OSC" onChange={e => setOsc(e.target.value)} />
            <button type="submit" className="btn btn--sm" disabled={!osc.trim() || osc.trim() === current?.osc}>ASIGNAR</button>
            {current?.osc && <button type="button" className="btn btn--sm" onClick={() => void remove({ osc: true }, 'OSC quitado')}>QUITAR</button>}
          </form>
          {current && (
            <>
              <div className="mapping-menu__row">
                <span className="mapping-menu__key">TOMA</span>
                {TAKEOVERS.map(t => (
                  <button key={t.id} type="button" title={t.title} className={`segmented__item${current.takeover === t.id ? ' is-active' : ''}`}
                    onClick={() => void assign({ takeover: t.id })}>{t.label}</button>
                ))}
              </div>
              <div className="mapping-menu__row">
                <span className="mapping-menu__key">BOTÓN</span>
                {BUTTONS.map(b => (
                  <button key={b.id} type="button" className={`segmented__item${current.button === b.id ? ' is-active' : ''}`}
                    onClick={() => void assign({ button: b.id })}>{b.label}</button>
                ))}
              </div>
              <RangeRow current={current} min={spec?.min} max={spec?.max} onChange={fields => void assign(fields)} />
              {alsoDriven.length > 0 && (
                <p className="mapping-menu__note">{sourceLabel(current)} también controla: {alsoDriven.map(a => `${a.paramId} (${refLabel(a)})`).join(', ')}</p>
              )}
              {!current.effective && <p className="mapping-menu__note">Oculto: un ámbito superior mapea este parámetro.</p>}
              <button type="button" className="btn btn--sm btn--danger mapping-menu__remove" onClick={() => void remove({}, 'Mapeo eliminado')}>ELIMINAR MAPEO</button>
            </>
          )}
        </>
      )}
    </div>
  );
}

function RangeRow({ current, min, max, onChange }: { current: Assignment; min?: number; max?: number; onChange: (f: Record<string, unknown>) => void }) {
  const [lo, setLo] = useState(String(current.min));
  const [hi, setHi] = useState(String(current.max));
  useEffect(() => { setLo(String(+current.min.toFixed(4))); setHi(String(+current.max.toFixed(4))); }, [current.min, current.max]);
  const commit = () => {
    const a = Number(lo), b = Number(hi);
    if (Number.isFinite(a) && Number.isFinite(b) && (a !== current.min || b !== current.max)) onChange({ min: a, max: b });
  };
  return (
    <div className="mapping-menu__row">
      <span className="mapping-menu__key">RANGO</span>
      <input className="input mapping-menu__num" aria-label="Mínimo" value={lo} onChange={e => setLo(e.target.value)} onBlur={commit}
        onKeyDown={e => { if (e.key === 'Enter') commit(); }} title={min !== undefined ? `mín. ${min}` : undefined} />
      <span className="muted">…</span>
      <input className="input mapping-menu__num" aria-label="Máximo" value={hi} onChange={e => setHi(e.target.value)} onBlur={commit}
        onKeyDown={e => { if (e.key === 'Enter') commit(); }} title={max !== undefined ? `máx. ${max}` : undefined} />
      <button type="button" className={`btn btn--sm${current.invert ? ' is-active' : ''}`} aria-pressed={current.invert}
        onClick={() => onChange({ invert: !current.invert })}>INVERTIR</button>
    </div>
  );
}
