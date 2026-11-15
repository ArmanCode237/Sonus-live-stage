// Rotary control. One component for every knob in the app.
//   drag vertically (Shift = fine) · wheel · double-click = default · arrows/PgUp/PgDn
import { memo, useCallback, useId, useRef } from 'react';
import { formatValue, fromNorm, toNorm, type ParamSpec } from '../params/spec';
import { useMapAttrs, type MapAttrs } from '../store/mappings';
import { useParam } from '../store/hooks';
import { MOD_DRAG_TYPE, connectMod, modSource, modTarget, setModDragging, useModConnections, useModDragging } from '../store/modMatrix';

/** Modulation arcs drawn outside the LED ring: from the knob value to knob + amount. */
export interface KnobMod { amount: number; bipolar: boolean; color: string }
const arcPoint = (t: number, r: number) => {
  const rad = ((-135 + Math.min(1, Math.max(0, t)) * 270) * Math.PI) / 180;
  return `${(40 + Math.sin(rad) * r).toFixed(2)} ${(40 - Math.cos(rad) * r).toFixed(2)}`;
};
const arcPath = (a: number, b: number, r: number) => {
  const lo = Math.min(1, Math.max(0, Math.min(a, b))), hi = Math.min(1, Math.max(0, Math.max(a, b)));
  if (hi - lo < 0.004) return '';
  return `M${arcPoint(lo, r)}A${r} ${r} 0 ${(hi - lo) * 270 > 180 ? 1 : 0} 1 ${arcPoint(hi, r)}`;
};

// LED dot ring: 15 dots from -135° to +135°, like a hardware encoder ring.
const DOTS = Array.from({ length: 15 }, (_, i) => {
  const t = i / 14;
  const rad = ((-135 + t * 270) * Math.PI) / 180;
  return { t, x: 40 + Math.sin(rad) * 35, y: 40 - Math.cos(rad) * 35 };
});
// Knurled skirt: 24 short grip lines.
const GRIP = Array.from({ length: 24 }, (_, i) => {
  const rad = (i / 24) * Math.PI * 2;
  return `M${(40 + Math.sin(rad) * 23.5).toFixed(2)} ${(40 - Math.cos(rad) * 23.5).toFixed(2)}L${(40 + Math.sin(rad) * 27).toFixed(2)} ${(40 - Math.cos(rad) * 27).toFixed(2)}`;
}).join('');

export interface KnobViewProps {
  /** data-param / badge attributes for MIDI-OSC mapping (see store/mappings). */
  mapAttrs?: MapAttrs;
  label: string;
  value: number | undefined;
  spec: ParamSpec | undefined;
  onChange: (value: number) => void;
  size?: 'sm' | 'md' | 'lg';
  /** Custom readout (defaults to the spec formatter). */
  format?: (value: number) => string;
  disabled?: boolean;
  title?: string;
  /** v32.3 modulation connections on this knob (rings) and drop-target state. */
  mods?: KnobMod[];
  modDrop?: 'ok' | 'voice-only' | null;
  onModDrop?: (source: number) => void;
}

export const KnobView = memo(function KnobView({ label, value, spec, onChange, size = 'md', format, disabled, title, mapAttrs, mods, modDrop, onModDrop }: KnobViewProps) {
  const labelId = useId();
  const drag = useRef<{ y: number; norm: number } | null>(null);
  const ready = spec !== undefined && value !== undefined;
  const norm = ready ? toNorm(spec, value) : 0;
  const origin = ready && spec.min < 0 && spec.max > 0 && !spec.log ? toNorm(spec, 0) : 0;
  const a = Math.min(origin, norm), b = Math.max(origin, norm);
  const text = !ready ? '—' : format ? format(value) : formatValue(spec, value);

  const commitNorm = useCallback((n: number) => {
    if (!spec) return;
    onChange(fromNorm(spec, n));
  }, [spec, onChange]);

  const onPointerDown = (e: React.PointerEvent<SVGSVGElement>) => {
    if (!ready || disabled || e.button !== 0) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    drag.current = { y: e.clientY, norm };
  };
  const onPointerMove = (e: React.PointerEvent<SVGSVGElement>) => {
    const d = drag.current;
    if (!d) return;
    const range = e.shiftKey ? 900 : 180;
    const next = Math.min(1, Math.max(0, d.norm + (d.y - e.clientY) / range));
    d.norm = next; d.y = e.clientY;
    commitNorm(next);
  };
  const endDrag = () => { drag.current = null; };
  const nudge = (steps: number, fine: boolean) => {
    if (!ready || disabled) return;
    if (spec.step) { onChange(Math.min(spec.max, Math.max(spec.min, value + steps * spec.step))); return; }
    commitNorm(norm + steps * (fine ? 0.002 : 0.01));
  };
  const onWheel = (e: React.WheelEvent) => { if (!disabled) nudge(e.deltaY < 0 ? 1 : -1, e.shiftKey); };
  const onKeyDown = (e: React.KeyboardEvent) => {
    const map: Record<string, number> = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 10, PageDown: -10 };
    if (e.key in map) { e.preventDefault(); nudge(map[e.key], e.shiftKey); }
    if (e.key === 'Home' && spec) { e.preventDefault(); onChange(spec.min); }
    if (e.key === 'End' && spec) { e.preventDefault(); onChange(spec.max); }
  };
  const reset = () => { if (ready && !disabled) onChange(spec.defaultValue); };

  return (
    <div className={`knob knob--${size}${disabled ? ' is-disabled' : ''}${modDrop ? ` is-mod-${modDrop}` : ''}${mods?.length ? ' has-mods' : ''}`} title={title} {...mapAttrs}
      onDragOver={onModDrop && modDrop === 'ok' ? e => { if (e.dataTransfer.types.includes(MOD_DRAG_TYPE)) { e.preventDefault(); e.dataTransfer.dropEffect = 'link'; } } : undefined}
      onDrop={onModDrop && modDrop === 'ok' ? e => { const s = e.dataTransfer.getData(MOD_DRAG_TYPE); if (s !== '') { e.preventDefault(); setModDragging(null); onModDrop(Number(s)); } } : undefined}>
      <svg
        className="knob__svg" viewBox="0 0 80 80" role="slider" tabIndex={disabled ? -1 : 0}
        aria-labelledby={labelId} aria-valuemin={spec?.min} aria-valuemax={spec?.max}
        aria-valuenow={value} aria-valuetext={text} aria-disabled={disabled || !ready}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={endDrag}
        onPointerCancel={endDrag} onLostPointerCapture={endDrag}
        onWheel={onWheel} onKeyDown={onKeyDown} onDoubleClick={reset}
      >
        <g className="knob__ring">
          {DOTS.map(d => {
            const lit = ready && d.t >= a - 0.035 && d.t <= b + 0.035 && (b - a > 0.001 || Math.abs(d.t - origin) < 0.035);
            return <circle key={d.t} className={lit ? 'knob__dot is-lit' : 'knob__dot'} cx={d.x} cy={d.y} r="2.6" />;
          })}
        </g>
        {ready && mods?.map((m, i) => {
          const d = arcPath(m.bipolar ? norm - m.amount : norm, norm + m.amount, 38.6 - i * 0.0);
          return d ? <path key={i} className="knob__mod" d={d} style={{ stroke: m.color }} /> : null;
        })}
        <circle className="knob__skirt" cx="40" cy="40" r="27.5" />
        <path className="knob__grip" d={GRIP} />
        <circle className="knob__cap" cx="40" cy="40" r="21" />
        <line className="knob__pointer" x1="40" y1="38" x2="40" y2="19" transform={`rotate(${-135 + norm * 270} 40 40)`} />
      </svg>
      <span className="knob__label" id={labelId}>{label}</span>
      <output className="knob__value">{text}</output>
    </div>
  );
});

export interface ParamKnobProps extends Omit<KnobViewProps, 'value' | 'spec' | 'onChange'> { id: string }

/** Knob bound to an engine parameter ID. */
export function ParamKnob({ id, ...rest }: ParamKnobProps) {
  const { value, spec, set } = useParam(id);
  const mapAttrs = useMapAttrs(id);
  const target = modTarget(id);
  const connections = useModConnections();
  const dragging = useModDragging();
  const mods = target ? connections.filter(c => c.target === id).map(c => ({ amount: c.amount, bipolar: c.bipolar, color: modSource(c.source).color })) : undefined;
  const modDrop = target && dragging !== null ? (modSource(dragging).voice && !target.voice ? 'voice-only' : 'ok') : null;
  return <KnobView value={value} spec={spec} onChange={set} mapAttrs={mapAttrs} mods={mods} modDrop={modDrop}
    onModDrop={target ? source => void connectMod(connections, source, id) : undefined} {...rest} />;
}
