// v32.4 visual output meters (reference only): peak level in dBFS on a -48..+6 dB scale,
// with a 1.5 s peak-hold tick, a numeric readout and a CLIP LED that latches for 2 s.
// Values come from the engine's live frames (modulePeaks / sourcePeaks, linear peak).
import { useRef } from 'react';
import { useEngine } from '../store/hooks';

const FLOOR = -48, CEIL = 6;
const toDb = (v: number) => 20 * Math.log10(Math.max(v, 1e-6));
const toPos = (db: number) => Math.min(1, Math.max(0, (db - FLOOR) / (CEIL - FLOOR)));
const TICKS = [-36, -24, -12, -6, 0];

/** Peak hold + clip latch kept between renders (no state: the live frames re-render us). */
function useHold(value: number) {
  const ref = useRef({ peak: 0, at: 0, clipAt: -1e9 });
  const now = performance.now();
  const h = ref.current;
  if (value >= h.peak || now - h.at > 1500) { h.peak = value; h.at = now; }
  if (value >= 1) h.clipAt = now;
  return { peak: h.peak, clip: now - h.clipAt < 2000 };
}

export function LevelMeter({ value, label, compact = false, title }: { value: number; label?: string; compact?: boolean; title?: string }) {
  const v = Number.isFinite(value) ? value : 0;
  const { peak, clip } = useHold(v);
  const db = toDb(v), peakDb = toDb(peak);
  const silent = peakDb <= FLOOR;
  return (
    <div className={`level-meter${compact ? ' level-meter--compact' : ''}${clip ? ' is-clip' : ''}`} role="meter"
      aria-valuemin={FLOOR} aria-valuemax={CEIL} aria-valuenow={Math.round(Math.max(FLOOR, db))}
      aria-label={`${label ?? 'Nivel'}: ${silent ? 'silencio' : `${peakDb.toFixed(1)} dBFS`}`} title={title ?? 'Pico de salida en dBFS (referencia visual)'}>
      {label && <span className="level-meter__label">{label}</span>}
      <span className="level-meter__track">
        <span className="level-meter__fill" style={{ clipPath: `inset(0 ${(1 - toPos(db)) * 100}% 0 0)` }} />
        {!silent && <span className="level-meter__hold" style={{ left: `${toPos(peakDb) * 100}%` }} />}
        {!compact && TICKS.map(t => <span key={t} className={`level-meter__tick${t === 0 ? ' is-zero' : ''}`} style={{ left: `${toPos(t) * 100}%` }} />)}
      </span>
      {!compact && <output className="level-meter__value">{silent ? '−∞' : peakDb.toFixed(1)}</output>}
      {!compact && <span className="level-meter__clip" aria-hidden="true">CLIP</span>}
    </div>
  );
}

/** Output of a module (after its Studio Rack, before the master). */
export function ModuleMeter({ module }: { module: 0 | 1 | 2 }) {
  const v = useEngine(s => s?.modulePeaks?.[module] ?? 0);
  return <LevelMeter value={v} label="OUT" title="Salida del módulo (después de su FX RACK, antes del master) · pico en dBFS" />;
}

/** One sound source: piano layer 0-4, synth layer 5-8, organ 9. */
export function SourceMeter({ source, compact = true, label }: { source: number; compact?: boolean; label?: string }) {
  const v = useEngine(s => s?.sourcePeaks?.[source] ?? 0);
  return <LevelMeter value={v} compact={compact} label={label} title="Salida de la capa (después de sus inserts) · pico en dBFS" />;
}
