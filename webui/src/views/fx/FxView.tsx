// Effects: four studio racks (EQ + five insert slots + per-channel sends) and
// the module effect chains (master, synth bus, organ bus).
import { useMemo, useState } from 'react';
import {
  FIELD_ENABLED, FIELD_TYPE, FIELD_VERSION, SHIMMER_ATMOSPHERE_VERSION, SHIMMER_FREEZE_PARAM, isShimmer, STUDIO_CHANNELS, STUDIO_EQ_BANDS, STUDIO_RACKS, STUDIO_SLOTS, STUDIO_TYPES,
  optionIndex, optionValue, slotControls, slotField, slotOptions, slotValueText, type StudioRack, type StudioSlot,
} from '../../data/studio';
import { NOTE_DIVISIONS, REVERB_TYPES, SHIMMER_COLOR_MODES, SHIMMER_PITCH_MODES, SHIMMER_SPATIAL_MODES } from '../../data/constants';
import { makeSpec, type ParamSpec } from '../../params/spec';
import { useEngine, useParam } from '../../store/hooks';
import { studioCommand, studioSet, useStudioValue } from '../../store/studio';
import { KnobView, ParamKnob } from '../../ui/Knob';
import { Btn, Panel, ParamSegmented, ParamSelect, ParamToggle, Row, Segmented, Select } from '../../ui/controls';
import { Link, useSearchParams } from 'react-router-dom';
import { EqScreen } from '../../ui/EqScreen';
import { FxEmblem } from '../../ui/FxEmblem';
import { slotEqModel, type EqModel } from '../../ui/eqResponse';

const SECTIONS = [...STUDIO_RACKS.map(r => `${r} RACK`), 'MODULE FX'];
const SLOPES = [12, 24, 36, 48];

const specs = {
  norm: makeSpec('norm', 0, 1),
  gain: makeSpec('eqGain', -15, 15),
  trim: makeSpec('trimDb', -24, 24),
  freq: makeSpec('eqFreq', 30, 16000),
  q: makeSpec('eqQ', 0.3, 12),
  hp: makeSpec('eqHP', 20, 1000),
  lp: makeSpec('eqLP', 1000, 20000),
};

function RackKnob({ k, label, spec, engineValue, body, format, size = 'sm' }: {
  k: string; label: string; spec: ParamSpec; engineValue: number | undefined;
  body: (v: number) => Record<string, unknown>; format?: (v: number) => string; size?: 'sm' | 'md';
}) {
  const value = useStudioValue(k, engineValue);
  return <KnobView label={label} spec={spec} value={value} size={size} format={format} onChange={v => studioSet(k, body(v), v)} />;
}

/** Live rack EQ values (the knob being dragged wins over the engine echo). */
function useRackEqModel(rack: number, cfg: StudioRack): EqModel {
  const v = (s: string, e: number | undefined) => useStudioValue(`eq-${rack}-${s}`, e);
  const inDb = v('in', cfg.eqInputDb), outDb = v('out', cfg.eqOutputDb);
  const hp = v('hp', cfg.eqHighPass), lp = v('lp', cfg.eqLowPass);
  const bands = STUDIO_EQ_BANDS.map((_, b) => ({
    gainDb: v(`g${b}`, cfg.eqDb[b]) ?? 0, freqHz: v(`f${b}`, cfg.eqFreq[b]) ?? 1000, q: v(`q${b}`, cfg.eqQ[b]) ?? 1,
  }));
  return {
    enabled: cfg.eqEnabled, hpHz: hp, lpHz: lp, hpSlope: cfg.eqHighPassSlope, lpSlope: cfg.eqLowPassSlope,
    bands, gainDb: (inDb ?? 0) + (outDb ?? 0),
  };
}

/** PARAMETRIC EQ slot (type 12): its curve from the slot's normalized parameters. */
function SlotEqScreen({ rack, slot }: { rack: number; slot: number }) {
  const p = Array.from({ length: 15 }, (_, i) => useParam(`studioSlot${rack}_${slot}_p${i}`).value ?? 0);
  const key = p.join(',');
  const model = useMemo(() => slotEqModel(p), [key]); // eslint-disable-line react-hooks/exhaustive-deps
  return <EqScreen model={model} title={`SLOT ${slot + 1} · EQ`} className="eq-screen--slot" />;
}

function RackEq({ rack, cfg }: { rack: number; cfg: StudioRack }) {
  const k = (s: string) => `eq-${rack}-${s}`;
  const param = (field: number) => (v: number) => ({ op: 'eqParam', rack, field, value: v });
  const model = useRackEqModel(rack, cfg);
  return (
    <section className="rack-eq">
      <header className="rack-eq__head">
        <button type="button" className={`led-switch${cfg.eqEnabled ? ' is-on' : ''}`} aria-pressed={cfg.eqEnabled}
          aria-label="EQ del rack" onClick={() => studioCommand({ op: 'eqEnabled', rack, value: !cfg.eqEnabled })} />
        <h3>PARAMETRIC EQ</h3>
        <Select label="Posición del EQ en la cadena" value={cfg.eqPosition}
          options={Array.from({ length: STUDIO_SLOTS + 1 }, (_, i) => ({ value: i, label: i === 0 ? 'PRE · antes del slot 1' : i === STUDIO_SLOTS ? 'POST · después del slot 5' : `entre slot ${i} y ${i + 1}` }))}
          onChange={v => studioCommand({ op: 'eqParam', rack, field: 4, value: Number(v) })} />
      </header>
      <EqScreen model={model} title={`${STUDIO_RACKS[rack]} · PARAMETRIC EQ`} className="eq-screen--rack" />
      <div className="rack-eq__body">
        <div className="knobs">
          <RackKnob k={k('in')} label="INPUT" spec={specs.trim} engineValue={cfg.eqInputDb} body={param(2)} />
          <RackKnob k={k('hp')} label="HIGH PASS" spec={specs.hp} engineValue={cfg.eqHighPass} body={param(0)} />
        </div>
        <Segmented label="Pendiente HP" options={SLOPES.map(String)} values={SLOPES} value={cfg.eqHighPassSlope}
          onChange={v => studioCommand({ op: 'eqParam', rack, field: 15, value: v })} />
        {STUDIO_EQ_BANDS.map((name, b) => (
          <div key={name} className="eq-band">
            <span className="eq-band__name">{name}</span>
            <RackKnob k={k(`g${b}`)} label="GAIN" spec={specs.gain} engineValue={cfg.eqDb[b]} body={v => ({ op: 'eq', rack, band: b, value: v })} />
            <RackKnob k={k(`f${b}`)} label="FREQ" spec={specs.freq} engineValue={cfg.eqFreq[b]} body={param(5 + b)} />
            <RackKnob k={k(`q${b}`)} label="Q" spec={specs.q} engineValue={cfg.eqQ[b]} body={param(10 + b)} />
          </div>
        ))}
        <Segmented label="Pendiente LP" options={SLOPES.map(String)} values={SLOPES} value={cfg.eqLowPassSlope}
          onChange={v => studioCommand({ op: 'eqParam', rack, field: 16, value: v })} />
        <div className="knobs">
          <RackKnob k={k('lp')} label="LOW PASS" spec={specs.lp} engineValue={cfg.eqLowPass} body={param(1)} />
          <RackKnob k={k('out')} label="OUTPUT" spec={specs.trim} engineValue={cfg.eqOutputDb} body={param(3)} />
        </div>
      </div>
    </section>
  );
}

/** Shimmer Atmosphere v5 starting point: every voice / tank control set to a musical,
 *  ambient default (≈11 s decay, +12 st with a quiet +7 st, 184 ms grains), FREEZE released. */
const SHIMMER_START: readonly [param: number, value: number][] = [
  [7, 0.70], [2, 0.55], [4, 0.8], [1, 0.45], [6, 0.75], [12, (2 + 0.01) / 5],
  [16, (7 + 24) / 48], [17, 0.30], [20, 0.80], [18, 0.85], [19, 0.5], [21, 0.5], [22, 0.65],
  [SHIMMER_FREEZE_PARAM, 0],
];
async function startShimmerV5(rack: number, slot: number): Promise<void> {
  for (const [p, v] of SHIMMER_START) if (!await studioCommand({ op: 'slot', rack, slot, field: slotField(p), value: v })) return;
  await studioCommand({ op: 'slot', rack, slot, field: FIELD_VERSION, value: SHIMMER_ATMOSPHERE_VERSION });
}

function SlotOptionSelect({ rack, slot, s, param, label, options }: { rack: number; slot: number; s: StudioSlot; param: number; label: string; options: readonly string[] }) {
  const change = async (index: number) => {
    const wasShimmer = isShimmer(s);
    if (!await studioCommand({ op: 'slot', rack, slot, field: slotField(param), value: optionValue(index, options.length) })) return;
    // Selecting SHIMMER in the algorithm list starts a fresh Atmosphere v5 shimmer.
    if (s.type === 6 && param === 5 && index === 3 && !wasShimmer) await startShimmerV5(rack, slot);
  };
  return (
    <label className="field">
      <span className="field__label">{label}</span>
      <Select label={label} value={optionIndex(s.params[param] ?? 0, options.length)}
        options={options.map((o, i) => ({ value: i, label: o }))} onChange={v => void change(Number(v))} />
    </label>
  );
}

function ShimmerControls({ rack, slot, s }: { rack: number; slot: number; s: StudioSlot }) {
  if (!isShimmer(s)) return null;
  if (s.version < SHIMMER_ATMOSPHERE_VERSION) {
    return (
      <div className="shimmer-upgrade">
        <p className="hint">Este shimmer usa un motor retirado y no suena. Configúralo de nuevo con Atmosphere v5.</p>
        <Btn small variant="primary" onClick={() => void startShimmerV5(rack, slot)}>NUEVO SHIMMER V5</Btn>
      </div>
    );
  }
  const frozen = (s.params[SHIMMER_FREEZE_PARAM] ?? 0) >= 0.5;
  return (
    <div className="shimmer-freeze">
      <Btn small led active={frozen} title="Mantiene la atmósfera indefinidamente; al soltar, la cola decae"
        onClick={() => studioCommand({ op: 'slot', rack, slot, field: slotField(SHIMMER_FREEZE_PARAM), value: frozen ? 0 : 1 })}>
        FREEZE
      </Btn>
      <span className="tag">ATMOSPHERE V5</span>
    </div>
  );
}

/** CSS key of a slot's effect (data-fx), e.g. "stereo-delay". */
const fxKey = (type: number) => (STUDIO_TYPES[type] ?? 'EMPTY').toLowerCase().replace(/[^a-z]+/g, '-');

function Slot({ rack, slot, s }: { rack: number; slot: number; s: StudioSlot }) {
  const controls = slotControls(s);
  const options = slotOptions(s);
  const setType = async (type: number) => {
    if (await studioCommand({ op: 'slot', rack, slot, field: FIELD_TYPE, value: type }) && type > 0)
      await studioCommand({ op: 'slot', rack, slot, field: FIELD_ENABLED, value: 1 });
  };
  const move = (dir: number) => studioCommand({ op: 'swap', rack, slot, target: slot + dir });
  return (
    <section className={`rack-slot${s.type === 0 ? ' is-empty' : ''}${s.enabled ? ' is-on' : ''}`} data-fx={fxKey(s.type)}>
      <header className="rack-slot__head">
        <button type="button" className={`led-switch${s.enabled ? ' is-on' : ''}`} disabled={s.type === 0} aria-pressed={s.enabled}
          aria-label={`Slot ${slot + 1} activo`} onClick={() => studioCommand({ op: 'slot', rack, slot, field: FIELD_ENABLED, value: s.enabled ? 0 : 1 })} />
        <span className="rack-slot__num">{slot + 1}</span>
        <FxEmblem type={s.type} />
        <button type="button" className="icon-btn" aria-label={`Mover slot ${slot + 1} a la izquierda`} disabled={slot === 0} onClick={() => move(-1)}>◀</button>
        <button type="button" className="icon-btn" aria-label={`Mover slot ${slot + 1} a la derecha`} disabled={slot === STUDIO_SLOTS - 1} onClick={() => move(1)}>▶</button>
      </header>
      <Select label={`Efecto del slot ${slot + 1}`} value={s.type} className="rack-slot__type"
        options={STUDIO_TYPES.map((t, i) => ({ value: i, label: t }))} onChange={v => setType(Number(v))} />
      {options.map(o => <SlotOptionSelect key={o.param} rack={rack} slot={slot} s={s} param={o.param} label={o.label} options={o.options} />)}
      <ShimmerControls rack={rack} slot={slot} s={s} />
      {s.type === 12 && <SlotEqScreen rack={rack} slot={slot} />}
      <div className="knobs knobs--slot">
        {controls.map(([param, label, unit]) => (
          <ParamKnob key={param} id={`studioSlot${rack}_${slot}_p${param}`} label={label} size="sm"
            format={v => slotValueText(v, unit, s.type, param, s.version)} />
        ))}
      </div>
      {s.type === 0 && <p className="hint">Elige un efecto</p>}
    </section>
  );
}

function Sends({ rack, cfg }: { rack: number; cfg: StudioRack }) {
  return (
    <section className="rack-sends">
      <h3>SENDS · canal → slot</h3>
      <table className="sends-table">
        <thead><tr><th scope="col">CANAL</th>{Array.from({ length: STUDIO_SLOTS }, (_, i) => <th key={i} scope="col">{i + 1} · {STUDIO_TYPES[cfg.slots[i]?.type ?? 0]}</th>)}</tr></thead>
        <tbody>
          {STUDIO_CHANNELS[rack].map((channel, source) => (
            <tr key={channel}>
              <th scope="row">{channel}</th>
              {Array.from({ length: STUDIO_SLOTS }, (_, slot) => (
                <td key={slot}><ParamKnob id={`studioSend${rack}_${source}_${slot}`} label="SEND" size="sm" /></td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}

export function RackView({ rack }: { rack: number }) {
  const racks = useEngine(s => s?.params?.studioRacks);
  const routing = useEngine(s => s?.params?.studioRoutingEnabled ?? true);
  const cfg = racks?.[rack];
  const [view, setView] = useState(0);
  if (!cfg) return <div className="view-loading">Esperando al motor…</div>;
  return (
    <div className="rack">
      <Row className="rack__bar">
        <Segmented label="Vista del rack" options={['EFFECTS', 'SENDS']} value={view} onChange={setView} />
        <Btn small led active={routing} onClick={() => studioCommand({ op: 'mode', value: !routing })}
          title="Activa el enrutado por racks de estudio">STUDIO ROUTING</Btn>
      </Row>
      {view === 0 ? (
        <div className="rack__effects">
          <RackEq rack={rack} cfg={cfg} />
          <div className="rack__slots">{cfg.slots.map((s, i) => <Slot key={i} rack={rack} slot={i} s={s} />)}</div>
        </div>
      ) : <Sends rack={rack} cfg={cfg} />}
    </div>
  );
}

// ---------------------------------------------------------------- module FX
function MasterFx() {
  const reverbType = useEngine(s => Math.round(Number(s?.params?.reverbType ?? 0)));
  return (
    <div className="fx-grid">
      <Panel title="CHORUS" power={{ id: 'chorusEnabled', label: 'Chorus' }}>
        <div className="knobs"><ParamKnob id="chorusRate" label="RATE" size="sm" /><ParamKnob id="chorusDepth" label="DEPTH" size="sm" /><ParamKnob id="chorusMix" label="MIX" size="sm" />
          <ParamKnob id="chorusWidth" label="WIDTH" size="sm" /><ParamKnob id="chorusFeedback" label="FDBK" size="sm" /><ParamKnob id="chorusTone" label="TONE" size="sm" />
          <ParamKnob id="chorusDelay" label="DELAY" size="sm" /><ParamKnob id="chorusVoices" label="VOICES" size="sm" /></div>
      </Panel>
      <Panel title="PHASER" power={{ id: 'phaserEnabled', label: 'Phaser' }}>
        <div className="knobs"><ParamKnob id="phaserRate" label="RATE" size="sm" /><ParamKnob id="phaserDepth" label="DEPTH" size="sm" /><ParamKnob id="phaserFeedback" label="FDBK" size="sm" /><ParamKnob id="phaserMix" label="MIX" size="sm" /></div>
      </Panel>
      <Panel title="FLANGER" power={{ id: 'flangerEnabled', label: 'Flanger' }}>
        <div className="knobs"><ParamKnob id="flangerRate" label="RATE" size="sm" /><ParamKnob id="flangerDepth" label="DEPTH" size="sm" /><ParamKnob id="flangerFeedback" label="FDBK" size="sm" /><ParamKnob id="flangerMix" label="MIX" size="sm" /></div>
      </Panel>
      <Panel title="TREMOLO" power={{ id: 'tremoloEnabled', label: 'Tremolo' }}>
        <div className="knobs"><ParamKnob id="tremoloRate" label="RATE" size="sm" /><ParamKnob id="tremoloDepth" label="DEPTH" size="sm" /></div>
      </Panel>
      <Panel title="DELAY" power={{ id: 'delayEnabled', label: 'Delay' }}>
        <Row wrap><ParamToggle id="delayPingPong" label="PING-PONG" small /><ParamToggle id="delayTempoSync" label="SYNC" small /><ParamSelect id="delayDivision" label="NOTA" options={NOTE_DIVISIONS} /></Row>
        <div className="knobs"><ParamKnob id="delayTimeMs" label="TIME" size="sm" /><ParamKnob id="delayFeedback" label="FDBK" size="sm" /><ParamKnob id="delayDamping" label="DAMP" size="sm" /><ParamKnob id="delayMix" label="MIX" size="sm" /></div>
      </Panel>
      <Panel title="REVERB" power={{ id: 'reverbEnabled', label: 'Reverb' }} className="fx-reverb">
        <ParamSegmented id="reverbType" label="Tipo de reverb" options={REVERB_TYPES} />
        <div className="knobs"><ParamKnob id="reverbMix" label="MIX" size="sm" /><ParamKnob id="reverbSize" label="SIZE" size="sm" /><ParamKnob id="reverbDamping" label="DAMP" size="sm" /><ParamKnob id="reverbPreDelayMs" label="PRE-DLY" size="sm" />
          {reverbType === 3 && <><ParamKnob id="reverbShimmer" label="SHIMMER" size="sm" /><ParamKnob id="shimmerShiftSt" label="SHIFT" size="sm" /><ParamKnob id="shimmerFeedback" label="FDBK" size="sm" />
            <ParamKnob id="shimmerDiffusion" label="DIFFUSE" size="sm" /><ParamKnob id="shimmerLowCutHz" label="LO CUT" size="sm" /><ParamKnob id="shimmerHighCutHz" label="HI CUT" size="sm" />
            <ParamKnob id="shimmerModRateHz" label="MOD RATE" size="sm" /><ParamKnob id="shimmerModDepth" label="MOD DEPTH" size="sm" /></>}
        </div>
        {reverbType === 3 && <Row wrap><ParamSelect id="shimmerPitchMode" label="PITCH" options={SHIMMER_PITCH_MODES} /><ParamSelect id="shimmerReverbMode" label="STEREO" options={SHIMMER_SPATIAL_MODES} /><ParamSelect id="shimmerColorMode" label="COLOR" options={SHIMMER_COLOR_MODES} /></Row>}
      </Panel>
      <Panel title="MASTER COLOR">
        <Row wrap><ParamToggle id="masterDriveOn" label="DRIVE" small /><ParamToggle id="masterAutoPanOn" label="AUTO-PAN" small /></Row>
        <div className="knobs"><ParamKnob id="masterDriveAmount" label="DRIVE" size="sm" /><ParamKnob id="masterAutoPanRate" label="PAN RATE" size="sm" /><ParamKnob id="masterAutoPanDepth" label="PAN DEPTH" size="sm" /></div>
      </Panel>
    </div>
  );
}

const MODULE_FX_GROUPS: [string, string, [string, string][]][] = [
  ['CHORUS', 'ChorusOn', [['ChorusRate', 'RATE'], ['ChorusDepth', 'DEPTH'], ['ChorusMix', 'MIX']]],
  ['PHASER', 'PhaserOn', [['PhaserRate', 'RATE'], ['PhaserDepth', 'DEPTH'], ['PhaserFeedback', 'FDBK'], ['PhaserMix', 'MIX']]],
  ['FLANGER', 'FlangerOn', [['FlangerRate', 'RATE'], ['FlangerDepth', 'DEPTH'], ['FlangerFeedback', 'FDBK'], ['FlangerMix', 'MIX']]],
  ['TREMOLO', 'TremoloOn', [['TremoloRate', 'RATE'], ['TremoloDepth', 'DEPTH']]],
  ['DELAY', 'DelayOn', [['DelayTime', 'TIME'], ['DelayFeedback', 'FDBK'], ['DelayMix', 'MIX']]],
  ['REVERB', 'ReverbOn', [['ReverbMix', 'MIX'], ['ReverbSize', 'SIZE']]],
  ['DRIVE', 'DriveOn', [['DriveAmount', 'AMOUNT']]],
  ['AUTO-PAN', 'AutoPanOn', [['AutoPanRate', 'RATE'], ['AutoPanDepth', 'DEPTH']]],
];

export function ModuleFx({ module }: { module: 'synth' | 'organ' }) {
  const prefix = `${module}Fx`;
  return (
    <div className="fx-grid">
      {MODULE_FX_GROUPS.map(([name, on, knobs]) => (
        <Panel key={name} title={name} power={{ id: prefix + on, label: name }}>
          {name === 'REVERB' && <ParamSegmented id={`${prefix}ReverbType`} label="Tipo de reverb" options={REVERB_TYPES} />}
          <div className="knobs">{knobs.map(([field, label]) => <ParamKnob key={field} id={prefix + field} label={label} size="sm" />)}</div>
        </Panel>
      ))}
      <Panel title="SENDS">
        {module === 'synth'
          ? Array.from({ length: 3 }, (_, l) => (
            <div key={l} className="knobs"><span className="eq-band__name">LAYER {'ABC'[l]}</span>
              <ParamKnob id={`synthSend${l}Mod`} label="MOD" size="sm" /><ParamKnob id={`synthSend${l}Delay`} label="DELAY" size="sm" /><ParamKnob id={`synthSend${l}Reverb`} label="REVERB" size="sm" /></div>))
          : <div className="knobs"><ParamKnob id="organSendMod" label="MOD" size="sm" /><ParamKnob id="organSendDelay" label="DELAY" size="sm" /><ParamKnob id="organSendReverb" label="REVERB" size="sm" /></div>}
      </Panel>
    </div>
  );
}

function ModuleFxView() {
  const [tab, setTab] = useState(0);
  return (
    <div className="module-fx">
      <Segmented label="Cadena de efectos" options={['MASTER', 'SYNTH BUS', 'ORGAN BUS']} value={tab} onChange={setTab} />
      {tab === 0 && <MasterFx />}{tab === 1 && <ModuleFx module="synth" />}{tab === 2 && <ModuleFx module="organ" />}
    </div>
  );
}

const MODULE_ROUTES = ['/piano', '/synth', '/organ'];

export default function FxView() {
  // ?rack=0..3 opens a rack directly (links from the module pages).
  const [params] = useSearchParams();
  const requested = Number(params.get('rack'));
  const [section, setSection] = useState(Number.isInteger(requested) && requested >= 0 && requested < SECTIONS.length ? requested : 3);
  const title = useMemo(() => SECTIONS[section], [section]);
  return (
    <div className="view view--fx">
      <nav className="subnav" aria-label="Secciones de efectos">
        {SECTIONS.map((s, i) => (
          <button key={s} type="button" className={`subnav__item${section === i ? ' is-active' : ''}`} aria-current={section === i} onClick={() => setSection(i)}>{s}</button>
        ))}
        <span className="subnav__title">{title}</span>
        {section < 3 && <Link className="btn btn--sm" to={`${MODULE_ROUTES[section]}?tab=fx`}>ABRIR EN {STUDIO_RACKS[section]} ↗</Link>}
      </nav>
      {section < 4 ? <RackView key={section} rack={section} /> : <ModuleFxView />}
    </div>
  );
}
