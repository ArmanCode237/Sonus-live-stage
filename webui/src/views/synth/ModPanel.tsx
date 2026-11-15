// v32.3 modulation UI: draggable source chips, the per-layer matrix, macro knobs and
// envelope graphs. Hold a chip and drop it on any knob of the layer (it glows while you
// drag); the coloured ring on the knob shows the modulation range (knob -> knob + amount).
import { useMemo, useState } from 'react';
import { useEngine, useParam } from '../../store/hooks';
import {
  MOD_CURVES, MOD_DRAG_TYPE, MOD_MAX, MOD_SOURCES, MOD_TARGETS, connectMod, modSource, modTarget, modTargetLabel,
  saveModConnections, setModDragging, useModConnections, type ModConnection,
} from '../../store/modMatrix';
import { ParamKnob } from '../../ui/Knob';
import { Btn, Empty, Select } from '../../ui/controls';

function RailChip({ id, sub, short }: { id: number; sub?: string; short?: string }) {
  const s = modSource(id);
  return (
    <span className={`mod-chip mod-rail__chip${s.voice ? ' mod-chip--voice' : ''}`} draggable style={{ ['--chip' as string]: s.color }}
      title={`${s.name}${sub ? ` (${sub})` : ''}: arrástralo a un knob${s.voice ? ' (por voz: CUTOFF, LEVEL, PAN, PITCH)' : ''}`}
      onDragStart={e => { e.dataTransfer.setData(MOD_DRAG_TYPE, String(s.id)); e.dataTransfer.effectAllowed = 'link'; setModDragging(s.id); }}
      onDragEnd={() => setModDragging(null)}>
      <span className="mod-chip__dot" aria-hidden="true" /><span className="mod-rail__name">{short ?? s.name}</span>
      {sub && <span className="mod-rail__sub">{sub}</span>}
    </span>
  );
}

function LiveMeter({ label, value, bipolar }: { label: string; value: number; bipolar?: boolean }) {
  const v = Math.max(bipolar ? -1 : 0, Math.min(1, value));
  const style = bipolar
    ? { left: `${50 + Math.min(0, v) * 50}%`, width: `${Math.abs(v) * 50}%` }
    : { left: '0%', width: `${v * 100}%` };
  return (
    <div className="mod-rail__meter" aria-label={`${label} ${Math.round(v * 100)} %`}>
      <span>{label}</span><span className={`mod-rail__bar${bipolar ? ' is-bipolar' : ''}`}><span style={style} /></span>
    </div>
  );
}

/** Vertical modulation rail: macros, draggable sources and live controller meters. */
export function ModRail({ layer }: { layer: number }) {
  const bend = useEngine(s => (s as { pitchBend?: number } | null)?.pitchBend ?? 0);
  const wheel = useEngine(s => (s as { modWheel?: number } | null)?.modWheel ?? 0);
  return (
    <aside className="mod-rail" aria-label="Fuentes de modulación (arrastra a un knob)">
      <div className="mod-rail__title">MACROS</div>
      <div className="mod-rail__macros">
        {[1, 2, 3, 4].map(m => (
          <div key={m} className="mod-rail__macro">
            <ParamKnob id={`synthMacro${m}`} label={`MACRO ${m}`} size="sm" />
            <RailChip id={11 + m} short={`⠿ M${m}`} />
          </div>
        ))}
      </div>
      <div className="mod-rail__title">LFO</div>
      {[7, 8, 9, 10].map(id => <RailChip key={id} id={id} sub={id - 7 === layer ? 'esta capa' : `capa ${'ABCD'[id - 7]}`} />)}
      <div className="mod-rail__title">CONTROL</div>
      {[0, 1, 2, 11].map(id => <RailChip key={id} id={id} />)}
      <div className="mod-rail__title">POR VOZ</div>
      {[3, 4, 5, 6].map(id => <RailChip key={id} id={id} />)}
      <div className="mod-rail__live">
        <LiveMeter label="PB" value={bend > 1.5 ? (bend - 8192) / 8192 : bend} bipolar />
        <LiveMeter label="MW" value={wheel > 1.5 ? wheel / 127 : wheel} />
      </div>
      <p className="mod-rail__hint">Arrastra a un knob de la capa {'ABCD'[layer]}</p>
    </aside>
  );
}

function LfoAutoOn({ source }: { source: number }) {
  // An LFO used as a source must run: show its switch next to the row.
  const lfo = source >= 7 && source <= 10 ? source - 6 : 0;
  const { value, set } = useParam(lfo ? `L${lfo}_lfoEnabled` : '__none__');
  if (!lfo || (value ?? 0) >= .5) return null;
  return <Btn small onClick={() => set(1)} title="Este LFO está apagado: no modula hasta encenderlo">ENCENDER LFO {'ABCD'[lfo - 1]}</Btn>;
}

/** Connections of one layer (amount, polarity, curve, remove) + a keyboard way to add one. */
export function ModMatrixPanel({ layer }: { layer: number }) {
  const all = useModConnections();
  const rows = useMemo(() => all.map((c, i) => ({ c, i })).filter(({ c }) => modTarget(c.target)?.layer === layer), [all, layer]);
  const [source, setSource] = useState(0);
  const [suffix, setSuffix] = useState('cut');
  const update = (index: number, patch: Partial<ModConnection>) =>
    void saveModConnections(all.map((c, i) => (i === index ? { ...c, ...patch } : c)), 'Modulación actualizada');
  const remove = (index: number) => void saveModConnections(all.filter((_, i) => i !== index), 'Conexión eliminada');
  return (
    <fieldset className="group group--wide mod-matrix"><legend>MOD MATRIX · CAPA {'ABCD'[layer]} ({all.length}/{MOD_MAX})</legend>
      {rows.length === 0 && <Empty>Sin conexiones: arrastra una fuente del riel MOD a cualquier knob.</Empty>}
      {rows.map(({ c, i }) => {
        const src = modSource(c.source);
        return (
          <div key={i} className="mod-row" style={{ ['--chip' as string]: src.color }}>
            <span className="mod-row__source"><span className="mod-chip__dot" aria-hidden="true" />{src.name}</span>
            <span className="mod-row__arrow" aria-hidden="true">→</span>
            <span className="mod-row__target">{modTargetLabel(c.target)}</span>
            <label className="mod-row__amount">
              <input type="range" min={-100} max={100} step={1} value={Math.round(c.amount * 100)} aria-label="Cantidad"
                onChange={e => update(i, { amount: Number(e.target.value) / 100 })} />
              <output>{c.amount > 0 ? '+' : ''}{Math.round(c.amount * 100)}%</output>
            </label>
            <button type="button" className={`mod-row__flag${c.bipolar ? ' is-on' : ''}`} aria-pressed={c.bipolar}
              title="BIPOLAR: oscila alrededor del knob (± cantidad)" onClick={() => update(i, { bipolar: !c.bipolar })}>±</button>
            <Select label="Curva" value={c.curve} options={MOD_CURVES.map((n, k) => ({ value: k, label: n }))} onChange={v => update(i, { curve: Number(v) })} />
            <LfoAutoOn source={c.source} />
            <button type="button" className="icon-btn" aria-label="Quitar conexión" onClick={() => remove(i)}>×</button>
          </div>
        );
      })}
      <div className="mod-row mod-row--add">
        <Select label="Fuente" value={source} options={MOD_SOURCES.map(s => ({ value: s.id, label: s.name }))} onChange={v => setSource(Number(v))} />
        <span className="mod-row__arrow" aria-hidden="true">→</span>
        <Select label="Destino" value={suffix} options={Object.entries(MOD_TARGETS).map(([k, n]) => ({ value: k, label: n }))} onChange={setSuffix} />
        <Btn small variant="primary" onClick={() => void connectMod(all, source, `L${layer + 1}_${suffix}`)}>CONECTAR</Btn>
        <span className="hint">Desde = valor del knob · hasta = knob + cantidad. ± oscila a ambos lados.</span>
      </div>
    </fieldset>
  );
}

/** Four macro knobs: modulation sources that MIDI learn can also drive. */
export function MacroKnobs() {
  return (
    <div className="knobs macro-knobs" aria-label="Macros">
      {[1, 2, 3, 4].map(m => <ParamKnob key={m} id={`synthMacro${m}`} label={`MACRO ${m}`} size="sm" />)}
    </div>
  );
}

/** ADSR curve of a layer envelope (seconds on a compressed time axis). */
export function EnvGraph({ ids, label }: { ids: [string, string, string, string]; label: string }) {
  const a = useParam(ids[0]).value ?? .01, d = useParam(ids[1]).value ?? .3, s = useParam(ids[2]).value ?? .7, r = useParam(ids[3]).value ?? .3;
  const t = (x: number) => Math.sqrt(Math.max(0, x));            // compress long times
  const hold = .35, total = t(a) + t(d) + hold + t(r);
  const w = 160, h = 48, px = (x: number) => (x / total) * (w - 8) + 4, py = (y: number) => h - 4 - y * (h - 10);
  const x1 = px(t(a)), x2 = px(t(a) + t(d)), x3 = px(t(a) + t(d) + hold), x4 = px(total);
  const path = `M${px(0)} ${py(0)}L${x1} ${py(1)}L${x2} ${py(s)}L${x3} ${py(s)}L${x4} ${py(0)}`;
  return (
    <svg className="env-graph" viewBox={`0 0 ${w} ${h}`} role="img" aria-label={`${label}: A ${a.toFixed(2)} s, D ${d.toFixed(2)} s, S ${(s * 100).toFixed(0)} %, R ${r.toFixed(2)} s`}>
      <path className="env-graph__fill" d={`${path}Z`} />
      <path className="env-graph__line" d={path} />
      {[x1, x2, x3].map((x, i) => <line key={i} className="env-graph__mark" x1={x} x2={x} y1={4} y2={h - 4} />)}
    </svg>
  );
}
