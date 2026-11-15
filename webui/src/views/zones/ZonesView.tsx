// Key zones: keyboard split for the three modules and their layers (5 piano layers,
// 4 synth layers). Every zone has LEARN LOW / LEARN HIGH: arm it, play a key.
// A layer never sounds outside its module: its stored range is kept as set, and the
// part outside the module's zone is shown hatched (the engine plays the overlap).
import { useEffect, useSyncExternalStore } from 'react';
import { Link } from 'react-router-dom';
import { PIANO_LAYER_COUNT, SYNTH_LAYER_COUNT, SYNTH_LAYER_NAMES, noteName } from '../../data/constants';
import { useEngine, useParam } from '../../store/hooks';
import { notify } from '../../store/notify';
import { Panel } from '../../ui/controls';

const LOW = 21, HIGH = 108;
const pct = (n: number) => ((Math.min(HIGH, Math.max(LOW, n)) - LOW) / (HIGH - LOW)) * 100;

// ---------------------------------------------------------------- learn (one armed target at a time)
interface LearnTarget { id: string; edge: 'low' | 'high'; name: string; bounds?: [number, number]; otherId: string; seq: number }
let armed: LearnTarget | null = null;
const listeners = new Set<() => void>();
const setArmed = (t: LearnTarget | null) => { armed = t; listeners.forEach(l => l()); };
const useArmed = () => useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => armed);

/** Mounted once per page: applies the next played key to the armed zone edge. */
function LearnListener() {
  const target = useArmed();
  const seq = useEngine(s => s?.midiSeq ?? 0);
  const note = useEngine(s => s?.lastMidiNote ?? -1);
  const { set: setValue } = useParam(target?.id ?? '__none__');
  const other = useParam(target?.otherId ?? '__none__');
  useEffect(() => {
    if (!target) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') { setArmed(null); notify('LEARN cancelado'); } };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [target]);
  useEffect(() => {
    if (!target || seq === target.seq || note < 0) return;
    if (target.bounds && (note < target.bounds[0] || note > target.bounds[1])) {
      notify(`${noteName(note)} está fuera de la zona del módulo (${noteName(target.bounds[0])}–${noteName(target.bounds[1])})`, 'error');
      setArmed({ ...target, seq });                         // stay armed: play another key
      return;
    }
    setValue(note);
    // Keep LOW <= HIGH: the opposite edge follows when it would be crossed.
    const o = Math.round(other.value ?? (target.edge === 'low' ? 127 : 0));
    if (target.edge === 'low' && note > o) other.set(note);
    if (target.edge === 'high' && note < o) other.set(note);
    notify(`${target.name} · ${target.edge === 'low' ? 'LOW' : 'HIGH'} = ${noteName(note)}`, 'ok');
    setArmed(null);
  }, [seq]);                                                 // eslint-disable-line react-hooks/exhaustive-deps
  return null;
}

function LearnButton({ id, otherId, edge, name, bounds }: { id: string; otherId: string; edge: 'low' | 'high'; name: string; bounds?: [number, number] }) {
  const target = useArmed();
  const seq = useEngine(s => s?.midiSeq ?? 0);
  const on = target?.id === id;
  return (
    <button type="button" className={`zone-learn${on ? ' is-armed' : ''}`} aria-pressed={on}
      title={on ? 'Toca una tecla (Esc cancela)' : `LEARN ${edge === 'low' ? 'LOW' : 'HIGH'}: toca la tecla del límite`}
      onClick={() => setArmed(on ? null : { id, otherId, edge, name, bounds, seq })}>
      <span className="led" aria-hidden="true" />LEARN
    </button>
  );
}

function NoteStepper({ id, label, learn }: { id: string; label: string; learn: React.ReactNode }) {
  const { value, set } = useParam(id);
  const v = Math.round(value ?? 0);
  return (
    <div className="note-stepper" role="group" aria-label={label}>
      <span className="note-stepper__label">{label}</span>
      <button type="button" className="icon-btn" aria-label={`${label} −12`} onClick={() => set(Math.max(0, v - 12))}>«</button>
      <button type="button" className="icon-btn" aria-label={`${label} −1`} onClick={() => set(Math.max(0, v - 1))}>−</button>
      <output className="note-stepper__value">{noteName(v)}</output>
      <button type="button" className="icon-btn" aria-label={`${label} +1`} onClick={() => set(Math.min(127, v + 1))}>+</button>
      <button type="button" className="icon-btn" aria-label={`${label} +12`} onClick={() => set(Math.min(127, v + 12))}>»</button>
      {learn}
    </div>
  );
}

function ZoneRow({ name, lowId, highId, tone, bounds }: { name: string; lowId: string; highId: string; tone: string; bounds?: [number, number] }) {
  const low = Math.round(useParam(lowId).value ?? 0);
  const high = Math.round(useParam(highId).value ?? 127);
  // Effective range = overlap with the module's zone (the stored range is kept as set).
  const effLow = bounds ? Math.max(low, bounds[0]) : low;
  const effHigh = bounds ? Math.min(high, bounds[1]) : high;
  const silent = effLow > effHigh;
  const clipped = bounds && (low < bounds[0] || high > bounds[1]);
  return (
    <div className={`zone-row${silent ? ' is-silent' : ''}`}>
      <span className="zone-row__name">{name}</span>
      <div className="zone-row__map" aria-hidden="true">
        {clipped && <div className={`zone-row__bar zone-row__bar--outside zone-row__bar--${tone}`} style={{ left: `${pct(low)}%`, width: `${Math.max(0.6, pct(high) - pct(low))}%` }} />}
        {!silent && <div className={`zone-row__bar zone-row__bar--${tone}`} style={{ left: `${pct(effLow)}%`, width: `${Math.max(0.6, pct(effHigh) - pct(effLow))}%` }} />}
      </div>
      <NoteStepper id={lowId} label="LOW" learn={<LearnButton id={lowId} otherId={highId} edge="low" name={name} bounds={bounds} />} />
      <NoteStepper id={highId} label="HIGH" learn={<LearnButton id={highId} otherId={lowId} edge="high" name={name} bounds={bounds} />} />
      {silent && <span className="zone-row__note">Fuera de la zona del módulo: esta capa no suena</span>}
      {!silent && clipped && <span className="zone-row__note">Suena {noteName(effLow)}–{noteName(effHigh)} (limitada por el módulo)</span>}
    </div>
  );
}

function Ruler() {
  const cs = Array.from({ length: 8 }, (_, i) => 24 + i * 12);
  return (
    <div className="zone-row zone-row--ruler" aria-hidden="true">
      <span />
      <div className="zone-row__map">{cs.map(n => <span key={n} className="zone-ruler__mark" style={{ left: `${pct(n)}%` }}>{noteName(n)}</span>)}</div>
      <span /><span />
    </div>
  );
}

const MODULES = [
  { key: 'piano', name: 'PIANO', route: '/piano' },
  { key: 'synth', name: 'SYNTH', route: '/synth' },
  { key: 'organ', name: 'ORGAN', route: '/organ' },
] as const;

/** One module's key zones: its keyboard range plus its layers' ranges. Used by ZONES and inside each module page. */
export function ModuleZones({ module, link, listener = true }: { module: 0 | 1 | 2; link?: boolean; listener?: boolean }) {
  const m = MODULES[module];
  const low = Math.round(useParam(`${m.key}ZoneLow`).value ?? 0);
  const high = Math.round(useParam(`${m.key}ZoneHigh`).value ?? 127);
  const bounds: [number, number] = [low, high];
  return (
    <Panel title={`${m.name} · KEYZONES`} actions={link ? <Link className="btn btn--sm" to={`${m.route}?tab=zones`}>ABRIR EN {m.name} ↗</Link> : undefined}>
      {listener && <LearnListener />}
      <Ruler />
      <ZoneRow name={m.name} lowId={`${m.key}ZoneLow`} highId={`${m.key}ZoneHigh`} tone={m.key} />
      {module === 0 && Array.from({ length: PIANO_LAYER_COUNT }, (_, l) => (
        <ZoneRow key={l} name={`L${l + 1}`} lowId={`pianoZone${l}Low`} highId={`pianoZone${l}High`} tone="piano" bounds={bounds} />
      ))}
      {module === 1 && Array.from({ length: SYNTH_LAYER_COUNT }, (_, l) => (
        <ZoneRow key={l} name={`LAYER ${SYNTH_LAYER_NAMES[l]}`} lowId={`L${l + 1}_keyLow`} highId={`L${l + 1}_keyHigh`} tone="synth" bounds={bounds} />
      ))}
    </Panel>
  );
}

/** Overview of every module's zones (useful when building global programs). */
export default function ZonesView() {
  return (
    <div className="view view--zones">
      <LearnListener />
      <ModuleZones module={0} link listener={false} />
      <ModuleZones module={1} link listener={false} />
      <ModuleZones module={2} link listener={false} />
    </div>
  );
}
