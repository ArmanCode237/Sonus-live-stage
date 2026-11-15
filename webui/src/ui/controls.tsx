// Panel controls shared by every view. Each parameter-bound variant only needs an ID.
import { memo, useRef, type ReactNode } from 'react';
import { formatValue, fromNorm, toNorm, type ParamSpec } from '../params/spec';
import { useParam } from '../store/hooks';
import { useMapAttrs, type MapAttrs } from '../store/mappings';

// ---------------------------------------------------------------- buttons
// Hardware button (Nord style): a square cap with a central oval and an LED, the text
// as a label underneath. Variants: panel (dark), primary (orange: GUARDAR / STORE),
// browse (grey: BROWSER), danger (red). Glyph-only buttons (◀ ▶ ★ ×) keep the compact
// classic look; `plain` forces it.
type BtnVariant = 'panel' | 'primary' | 'browse' | 'ghost' | 'danger';
export interface BtnProps extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: BtnVariant;
  active?: boolean;
  /** Lights the status LED when active (toggles). Every hardware button has an LED. */
  led?: boolean;
  small?: boolean;
  plain?: boolean;
}
const isGlyph = (children: ReactNode) => typeof children === 'string' && children.trim().length <= 2 && !/[\p{L}\p{N}]/u.test(children);

export function Btn({ variant = 'panel', active, led, small, plain, className = '', children, ...rest }: BtnProps) {
  const hardware = !plain && variant !== 'ghost' && !isGlyph(children);
  const cls = ['btn', `btn--${variant}`, hardware ? 'hwbtn' : '', active ? 'is-active' : '', small ? 'btn--sm' : '', className].filter(Boolean).join(' ');
  if (!hardware) {
    return (
      <button type="button" className={cls} aria-pressed={active === undefined ? undefined : active} {...rest}>
        {led && <span className={`led${active ? ' is-on' : ''}`} aria-hidden="true" />}
        {children}
      </button>
    );
  }
  return (
    <button type="button" className={cls} aria-pressed={active === undefined ? undefined : active} {...rest}>
      <span className="hwbtn__cap" aria-hidden="true">
        <span className="hwbtn__oval" />
        <span className={`led hwbtn__led${active ? ' is-on' : ''}`} />
      </span>
      <span className="hwbtn__label">{children}</span>
    </button>
  );
}

export function ParamToggle({ id, label, title, small }: { id: string; label: string; title?: string; small?: boolean }) {
  const { value, set } = useParam(id);
  const on = (value ?? 0) >= 0.5;
  const mapAttrs = useMapAttrs(id);
  return <Btn led active={on} small={small} title={title} onClick={() => set(on ? 0 : 1)} {...mapAttrs}>{label}</Btn>;
}

/** Just the LED, as a compact power switch in panel headers. */
export function ParamLed({ id, label }: { id: string; label: string }) {
  const { value, set } = useParam(id);
  const on = (value ?? 0) >= 0.5;
  const mapAttrs = useMapAttrs(id);
  return (
    <button type="button" className={`led-switch${on ? ' is-on' : ''}`} aria-pressed={on} {...mapAttrs}
      aria-label={`${label}: ${on ? 'activado' : 'desactivado'}`} title={label} onClick={() => set(on ? 0 : 1)} />
  );
}

// ---------------------------------------------------------------- selectors
export interface SegmentedProps {
  options: readonly string[];
  value: number | undefined;
  onChange: (index: number) => void;
  /** Engine values for each option when they are not 0..n-1. */
  values?: readonly number[];
  label?: string;
  wrap?: boolean;
  mapAttrs?: MapAttrs;
}
export const Segmented = memo(function Segmented({ options, value, onChange, values, label, wrap, mapAttrs }: SegmentedProps) {
  return (
    <div className={`segmented${wrap ? ' segmented--wrap' : ''}`} role="radiogroup" aria-label={label} {...mapAttrs}>
      {options.map((text, i) => {
        const v = values ? values[i] : i;
        const on = value !== undefined && Math.round(value) === v;
        return (
          <button key={text + i} type="button" role="radio" aria-checked={on}
            className={`segmented__item${on ? ' is-active' : ''}`} onClick={() => onChange(v)}>{text}</button>
        );
      })}
    </div>
  );
});
export function ParamSegmented({ id, ...rest }: Omit<SegmentedProps, 'value' | 'onChange'> & { id: string }) {
  const { value, set } = useParam(id);
  const mapAttrs = useMapAttrs(id);
  return <Segmented value={value} onChange={set} label={rest.label ?? id} mapAttrs={mapAttrs} {...rest} />;
}

/**
 * Hardware-style option list: each option is a label with a red LED; the
 * active one is lit. Clicking a label selects it, the button cycles.
 */
export function LedChoice({ options, value, onChange, values, label, cycle = true, mapAttrs }: SegmentedProps & { cycle?: boolean }) {
  const vals = values ?? options.map((_, i) => i);
  const current = value === undefined ? -1 : vals.indexOf(Math.round(value));
  return (
    <div className="led-choice" role="radiogroup" aria-label={label} {...mapAttrs}>
      <div className="led-choice__list">
        {options.map((text, i) => (
          <button key={text + i} type="button" role="radio" aria-checked={current === i}
            className={`led-choice__item${current === i ? ' is-on' : ''}`} onClick={() => onChange(vals[i])}>
            <span className={`led${current === i ? ' is-on' : ''}`} aria-hidden="true" />{text}
          </button>
        ))}
      </div>
      {cycle && <button type="button" className="rubber" aria-label={`${label}: siguiente`}
        onClick={() => onChange(vals[(current + 1 + vals.length) % vals.length])} />}
      {label && <span className="led-choice__label">{label}</span>}
    </div>
  );
}
export function ParamLedChoice({ id, ...rest }: Omit<SegmentedProps, 'value' | 'onChange'> & { id: string; cycle?: boolean }) {
  const { value, set } = useParam(id);
  const mapAttrs = useMapAttrs(id);
  return <LedChoice value={value} onChange={set} mapAttrs={mapAttrs} {...rest} />;
}

export function Select({ value, options, onChange, label, className = '' }: {
  value: number | string | undefined; options: readonly { value: number | string; label: string }[];
  onChange: (value: string) => void; label: string; className?: string;
}) {
  return (
    <select className={`select ${className}`} aria-label={label} value={value ?? ''} onChange={e => onChange(e.target.value)}>
      {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
    </select>
  );
}
export function ParamSelect({ id, options, label }: { id: string; options: readonly string[]; label: string }) {
  const { value, set } = useParam(id);
  const mapAttrs = useMapAttrs(id);
  return (
    <label className="field" {...mapAttrs}>
      <span className="field__label">{label}</span>
      <Select label={label} value={value === undefined ? undefined : Math.round(value)}
        options={options.map((text, i) => ({ value: i, label: text }))} onChange={v => set(Number(v))} />
    </label>
  );
}

// ---------------------------------------------------------------- faders
export interface FaderViewProps {
  label: string; value: number | undefined; spec: ParamSpec | undefined; onChange: (v: number) => void;
  /** Drawbar style: value grows downwards, like a pulled organ drawbar. */
  drawbar?: boolean; format?: (v: number) => string; color?: 'brown' | 'white' | 'black';
  /** Kept for callers: every level fader now shows its LED strip (Nord style). */
  ladder?: boolean;
  mapAttrs?: MapAttrs;
}
const LADDER_SEGMENTS = 16;
export const FaderView = memo(function FaderView({ label, value, spec, onChange, drawbar, format, color, ladder, mapAttrs }: FaderViewProps) {
  const track = useRef<HTMLDivElement>(null);
  const ready = spec !== undefined && value !== undefined;
  const norm = ready ? toNorm(spec, value) : 0;
  const fromPointer = (clientY: number) => {
    const el = track.current;
    if (!el || !spec) return;
    const r = el.getBoundingClientRect();
    let n = (clientY - r.top) / r.height;
    if (!drawbar) n = 1 - n;
    onChange(fromNorm(spec, n));
  };
  const onPointerDown = (e: React.PointerEvent) => {
    if (!ready) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    fromPointer(e.clientY);
  };
  const onPointerMove = (e: React.PointerEvent) => { if (e.currentTarget.hasPointerCapture(e.pointerId)) fromPointer(e.clientY); };
  const onKeyDown = (e: React.KeyboardEvent) => {
    if (!ready) return;
    const dir = { ArrowUp: drawbar ? -1 : 1, ArrowDown: drawbar ? 1 : -1 }[e.key];
    if (!dir) return;
    e.preventDefault();
    if (spec.step) onChange(Math.min(spec.max, Math.max(spec.min, value + dir * spec.step)));
    else onChange(fromNorm(spec, norm + dir * 0.02));
  };
  const text = !ready ? '—' : format ? format(value) : formatValue(spec, value);
  const pos = 1 - norm;
  return (
    <div className={`fader${drawbar ? ' fader--drawbar' : ' fader--ladder'}${ladder ? ' fader--level' : ''}${color ? ` fader--${color}` : ''}`} {...mapAttrs}>
      <span className="fader__label">{label}</span>
      <div className="fader__body">
      {!drawbar && (
        // Square LED strip beside the slot: lights up to the fader position.
        <div className="fader__ladder" aria-hidden="true">
          {Array.from({ length: LADDER_SEGMENTS }, (_, i) => {
            const seg = LADDER_SEGMENTS - 1 - i;
            return <span key={i} className={`fader__seg${norm * LADDER_SEGMENTS > seg + 0.05 ? ' is-lit' : ''}`} />;
          })}
        </div>
      )}
      {drawbar && (
        // Drawbar scale 0-8 (pulled out = louder).
        <div className="fader__scale" aria-hidden="true">
          {Array.from({ length: 9 }, (_, i) => <span key={i} className={`fader__tick${Math.round(norm * 8) >= i && i > 0 ? ' is-lit' : ''}`}>{i}</span>)}
        </div>
      )}
      <div ref={track} className="fader__track" role="slider" tabIndex={0} aria-label={label}
        aria-valuemin={spec?.min} aria-valuemax={spec?.max} aria-valuenow={value} aria-valuetext={text}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onKeyDown={onKeyDown}
        onDoubleClick={() => ready && onChange(spec.defaultValue)}>
        {/* Drawbar: the cap travels inside the track (pulled out = lower), the rod follows it. */}
        {drawbar && <div className="fader__rod" style={{ height: `calc(${norm} * (100% - 28px))` }} />}
        {!drawbar && <div className="fader__fill" style={{ height: `${norm * 100}%` }} />}
        {!drawbar && <div className="fader__ticks" aria-hidden="true" />}
        <div className="fader__cap" style={{ top: drawbar ? `calc(${norm} * (100% - 28px))` : `${pos * 100}%` }} />
      </div>
      </div>
      <output className="fader__value">{text}</output>
    </div>
  );
});
export function ParamFader({ id, ...rest }: Omit<FaderViewProps, 'value' | 'spec' | 'onChange'> & { id: string }) {
  const { value, spec, set } = useParam(id);
  const mapAttrs = useMapAttrs(id);
  return <FaderView value={value} spec={spec} onChange={set} mapAttrs={mapAttrs} {...rest} />;
}

// ---------------------------------------------------------------- layout
/**
 * Panel variants follow the hardware: `section` is a graphite instrument
 * section (PIANO, SYNTH, ORGAN); `chassis` sits directly on the red body with
 * a dark label strip (PROGRAM, MASTER, LIBRARY…).
 */
export function Panel({ title, power, actions, children, className = '', accent, variant = 'section' }: {
  title: ReactNode; power?: { id: string; label: string }; actions?: ReactNode;
  children: ReactNode; className?: string; accent?: boolean; variant?: 'section' | 'chassis';
}) {
  return (
    <section className={`panel panel--${variant}${accent ? ' panel--accent' : ''} ${className}`}>
      <header className="panel__head">
        {power && <ParamLed id={power.id} label={power.label} />}
        <h2 className="panel__title">{title}</h2>
        {actions && <div className="panel__actions">{actions}</div>}
      </header>
      <div className="panel__body">{children}</div>
    </section>
  );
}

export function Row({ children, className = '', wrap }: { children: ReactNode; className?: string; wrap?: boolean }) {
  return <div className={`row${wrap ? ' row--wrap' : ''} ${className}`}>{children}</div>;
}

export function Lcd({ children, className = '' }: { children: ReactNode; className?: string }) {
  return <div className={`lcd ${className}`}>{children}</div>;
}

export function Meter({ left, right, clip }: { left: number; right: number; clip?: boolean }) {
  const pct = (v: number) => `${Math.min(100, Math.max(0, Math.sqrt(Math.max(0, v)) * 100))}%`;
  return (
    <div className={`meter${clip ? ' is-clip' : ''}`} aria-label="Nivel de salida" role="img">
      <div className="meter__bar"><div style={{ width: pct(left) }} /></div>
      <div className="meter__bar"><div style={{ width: pct(right) }} /></div>
    </div>
  );
}

export function Field({ label, children }: { label: string; children: ReactNode }) {
  return <label className="field"><span className="field__label">{label}</span>{children}</label>;
}

export function Empty({ children }: { children: ReactNode }) {
  return <p className="empty">{children}</p>;
}
