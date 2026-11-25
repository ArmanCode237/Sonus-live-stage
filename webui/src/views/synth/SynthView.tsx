// Synth: factory/user browser, global controls and the four-layer voice editor.
import { useEffect, useState } from 'react';
import { api } from '../../api/client';
import type { NamedPreset } from '../../api/types';
import {
  FM4_ALGORITHMS, SYNTH_FILTER_TYPES, SYNTH_TRANSIENTS, SYNTH_GLIDE_MODES, SYNTH_LAYER_COUNT, SYNTH_LAYER_NAMES, SYNTH_LFO_WAVES,
  SYNTH_OSC2_WAVES, SYNTH_SYNTHESIS_TYPES, SYNTH_VOICE_MODES, SYNTH_WAVES, SYNTH_WAVETABLES,
  SYNTH_WHEEL_MODES, noteName,
} from '../../data/constants';
import { SYNTH_FACTORY } from '../../data/synthFactory';
import { useEngine, useParam } from '../../store/hooks';
import {
  displayName, isEdited, restoreFactoryOriginal, saveFactoryEdit, toggleFavorite, useFactoryMeta,
} from '../../store/synthFactory';
import { Modal } from '../../ui/Modal';
import { DirtyTag } from '../../ui/DirtyTag';
import { runAction } from '../../store/notify';
import { ParamKnob } from '../../ui/Knob';
import { Btn, Field, Lcd, ParamLed, ParamSegmented, ParamSelect, ParamToggle, Row, Segmented } from '../../ui/controls';
import { InsertChain } from '../shared/InsertChain';
import { ModuleTabs } from '../shared/ModuleTabs';
import { SourceMeter } from '../../ui/LevelMeter';
import { useSynthPresetRef } from '../../store/synthUserPatch';
import { SynthEq } from './SynthEq';
import { Dx7Browser } from './Dx7Browser';
import { Mt32Browser } from './Mt32Browser';
import { SynthSoundBrowser } from '../../browser/ModuleBrowsers';
import { EnvGraph, ModMatrixPanel, ModRail } from './ModPanel';
import { FilterScreen, LfoScreen, WaveScreen } from './SynthScreens';

// v32.4 workspace: patch bar (browser overlay) · layer tabs A-D · modulation rail ·
// one-screen editor (engine / filter + envelopes) · bottom deck for the rest.
const DECK_TABS = ['LFO', 'MATRIZ', 'EXPRESIÓN', 'VOICE', 'SEQ', 'INSERTS'] as const;

// ---------------------------------------------------------------- browser
// The engine tracks the preset name (state.presetRefs.synth): a factory load clears it.
const loadFactory = (id: number, label: string) => runAction(() => api.loadSynthFactory(id), label, true);

/** GUARDAR: a NEW user preset (default) or the edit over the current factory slot. */
function SaveSynthDialog({ id, defaultName, edited, onClose }: { id: number; defaultName: string; edited: boolean; onClose: () => void }) {
  const [mode, setMode] = useState(0);
  const [name, setName] = useState(defaultName);
  const [userPresets, setUserPresets] = useState<NamedPreset[] | null>(null);
  useEffect(() => { api.synthPresets().then(setUserPresets).catch(() => setUserPresets([])); }, []);
  const trimmed = name.trim();
  const exists = mode === 0 && Boolean(userPresets?.some(p => p.name === trimmed));
  const save = async () => {
    if (mode === 1) { if (await saveFactoryEdit(id, trimmed)) onClose(); return; }
    const r = await runAction(() => api.saveSynthPreset(trimmed, exists), exists ? `«${trimmed}» sobrescrito` : `«${trimmed}» guardado en USUARIO`);
    if (r) onClose();
  };
  const restore = async () => { if (await restoreFactoryOriginal(id)) onClose(); };
  const slot = String(id).padStart(3, '0');
  return (
    <Modal title="GUARDAR PRESET DEL SYNTH" onClose={onClose}>
      <form className="form" onSubmit={e => { e.preventDefault(); if (trimmed) void save(); }}>
        <Segmented label="Destino" options={['NUEVO PRESET (USUARIO)', `SOBRE FÁBRICA ${slot}`]} value={mode} onChange={setMode} />
        <Field label="Nombre">
          <input className="input" autoFocus maxLength={64} value={name} onChange={e => setName(e.target.value)} aria-label="Nombre del preset" />
        </Field>
        <p className="hint">{mode === 0
          ? (exists ? `Ya existe un preset de usuario «${trimmed}»: GUARDAR lo sobrescribe.` : 'Se crea un preset nuevo en BROWSER → USUARIO. El preset de fábrica no se modifica.')
          : 'Guarda la edición sobre este preset de fábrica. El original se conserva y puede restaurarse.'}</p>
        <Row>
          {mode === 1 && edited && <Btn variant="danger" onClick={() => void restore()}>RESTAURAR ORIGINAL</Btn>}
          <span className="grow" />
          <Btn onClick={onClose}>CANCELAR</Btn>
          <Btn variant="primary" type="submit" disabled={!trimmed || (mode === 0 && userPresets === null)}>{exists ? 'SOBRESCRIBIR' : 'GUARDAR'}</Btn>
        </Row>
      </form>
    </Modal>
  );
}

/** Patch bar: program display, browse, save and the synth-wide controls. */
function PatchBar({ layer }: { layer: number }) {
  const presetId = Math.round(useEngine(s => s?.params?.synthPresetId ?? 1));
  const { favorites, overrides } = useFactoryMeta();
  const [browsing, setBrowsing] = useState(false);
  const [saving, setSaving] = useState(false);
  const current = SYNTH_FACTORY[presetId - 1];
  const edited = isEdited(presetId, overrides);
  const total = SYNTH_FACTORY.length;
  const step = (d: number) => loadFactory(((presetId - 1 + d + total) % total) + 1, 'Synth');
  const user = useSynthPresetRef();
  const showUser = user !== null;
  return (
    <section className="patch-bar" aria-label="Patch del sintetizador">
      <ParamToggle id="synthEnabled" label="SYNTH" small />
      <Btn small onClick={() => void step(-1)} aria-label="Patch anterior">◀</Btn>
      <Lcd className="patch-lcd patch-bar__lcd">
        <div className="patch-lcd__top">{showUser
          ? <><span>{user.kind === 'USER' ? 'USER PRESET' : user.kind}</span><span><DirtyTag module={1} /> {user.kind === 'USER' ? 'USUARIO' : user.kind}</span></>
          : <><span>PATCH {String(presetId).padStart(3, '0')} / {total}</span><span><DirtyTag module={1} /> {edited ? '✎ USUARIO · ' : ''}{current?.category}</span></>}</div>
        <div className="patch-lcd__name">{showUser ? user.name : displayName(current, overrides)}</div>
      </Lcd>
      <Btn small onClick={() => void step(1)} aria-label="Patch siguiente">▶</Btn>
      <Btn small variant="browse" onClick={() => setBrowsing(true)} title="Fábrica, usuario, D-50, DX7 y MT-32">BROWSER</Btn>
      <Btn small led active={favorites.has(presetId)} onClick={() => void toggleFavorite(presetId)} title="Favorito">★</Btn>
      <Btn small variant="primary" onClick={() => setSaving(true)} title="Guardar como preset nuevo de usuario o sobre el preset de fábrica">GUARDAR</Btn>
      <div className="patch-bar__knobs knobs">
        <ParamKnob id="synthMasterVol" label="LEVEL" size="sm" /><ParamKnob id="synthOutGainDb" label="GAIN" size="sm" />
        <ParamKnob id="synthMacroCutoff" label="BRIGHT" size="sm" />
        <ParamKnob id="synthPan" label="PAN" size="sm" /><ParamKnob id="synthTune" label="TUNE" size="sm" />
        <ParamKnob id="synthWheelGlobalCutoff" label="WHL CUT" size="sm" /><ParamKnob id="synthWheelGlobalAmpDb" label="WHL AMP" size="sm" />
      </div>
      {browsing && <SynthSoundBrowser layer={layer} onClose={() => setBrowsing(false)} />}
      {saving && <SaveSynthDialog id={presetId} defaultName={showUser ? user.name : displayName(current, overrides)} edited={edited} onClose={() => setSaving(false)} />}
    </section>
  );
}

// ---------------------------------------------------------------- layer tabs
/** What the OSC 2 level knob does on waveforms that reuse it. */
const OSC2_ROLE: Record<number, string> = {
  14: 'OSC2 = cantidad de plegado', 29: 'OSC2 = DCW (sigue la envolvente de filtro)', 30: 'OSC2 = DCW resonante (sigue la envolvente de filtro)',
  31: 'SAW = VCO1 · OSC2 = VCO2 · SUB = VCO3 (−1 oct)', 32: 'OSC2 = barrido de sync por la envolvente de filtro', 33: 'OSC2 = apertura de las 7 sierras',
};

/** Layer output: LEVEL (into filter / inserts), PAN and GAIN (trim after the inserts). */
function OutKnobs({ L }: { L: string }) {
  return (
    <div className="knobs out-knobs">
      <ParamKnob id={`${L}vol`} label="LEVEL" size="sm" /><ParamKnob id={`${L}pan`} label="PAN" size="sm" />
      <ParamKnob id={`${L}gain`} label="GAIN" size="sm" />
    </div>
  );
}

// Engines without the MIXER card show the layer output inside ENGINE.
const NO_MIXER = new Set([3, 5, 6]);

function SynthesisSelect({ L, type }: { L: string; type: number }) {
  const { value: wave } = useParam(`${L}wave`);
  const w = Math.round(wave ?? 0);
  return (
    <fieldset className="group engine-card"><legend>ENGINE</legend>
      <ParamSelect id={`${L}synthType`} label="SÍNTESIS" options={SYNTH_SYNTHESIS_TYPES} />
      {type === 0 && <>
        <ParamSelect id={`${L}wave`} label="FORMA DE ONDA" options={SYNTH_WAVES} />
        {OSC2_ROLE[w] && <p className="hint">{OSC2_ROLE[w]}</p>}
      </>}
      {(type === 2 || type === 4) && <ParamSelect id={`${L}wtIdx`} label="WAVETABLE" options={SYNTH_WAVETABLES} />}
      <WaveScreen L={L} />
      {NO_MIXER.has(type) && <OutKnobs L={L} />}
    </fieldset>
  );
}

function Fm4({ L }: { L: string }) {
  return (
    <>
      <fieldset className="group"><legend>FM4 · ALGORITHM</legend>
        <ParamSelect id={`${L}fm4Algo`} label="ALGORITMO" options={FM4_ALGORITHMS} />
        <div className="knobs">
          <ParamKnob id={`${L}fm4Fb`} label="FEEDBACK" size="sm" /><ParamKnob id={`${L}fm4Vel`} label="VEL→MOD" size="sm" />
        </div>
        <p className="hint">OP4 tiene feedback. Los moduladores usan LEVEL como índice; los carriers, como volumen.</p>
      </fieldset>
      {[1, 2, 3, 4].map(op => (
        <fieldset key={op} className="group"><legend>OP {op}</legend>
          <div className="knobs">
            <ParamKnob id={`${L}op${op}Ratio`} label="RATIO" size="sm" /><ParamKnob id={`${L}op${op}Level`} label="LEVEL" size="sm" />
            <ParamKnob id={`${L}op${op}Atk`} label="ATTACK" size="sm" /><ParamKnob id={`${L}op${op}Dec`} label="DECAY" size="sm" />
            <ParamKnob id={`${L}op${op}Sus`} label="SUSTAIN" size="sm" />
          </div>
        </fieldset>
      ))}
    </>
  );
}

function Osc({ L }: { L: string }) {
  const { value: typeValue } = useParam(`${L}synthType`);
  const type = Math.round(typeValue ?? 0);
  if (type === 3) return <div className="editor-grid editor-grid--3"><SynthesisSelect L={L} type={type} /><Fm4 L={L} /></div>;
  if (type === 5) return <div className="editor-grid editor-grid--3"><SynthesisSelect L={L} type={type} /><Dx7Browser layer={Number(L.slice(1, -1)) - 1} /></div>;
  if (type === 6) return <div className="editor-grid editor-grid--3"><SynthesisSelect L={L} type={type} /><Mt32Browser layer={Number(L.slice(1, -1)) - 1} /></div>;
  return (
    <div className="editor-grid editor-grid--3">
      <SynthesisSelect L={L} type={type} />
      {type === 7 && (
        <fieldset className="group"><legend>LA32 · PARTIALS</legend>
          <div className="knobs"><ParamKnob id={`${L}laRing`} label="RING" size="sm" /></div>
          <p className="hint" title="Generador de onda del chip LA32 (MT-32 / D-50). Parcial 1 = SAW o PULSE del mixer (con PW), parcial 2 = OSC 2. El FILTER es la TVF: su cutoff, resonancia, key track y envolvente cambian la forma de onda, como en el LA32. El ataque PCM se aproxima con ATTACK TRANSIENT (pestaña EXPRESIÓN).">
            P1 = SAW/PULSE · P2 = OSC 2 · FILTER = TVF</p>
        </fieldset>
      )}
      {type === 4 && (
        <fieldset className="group"><legend>LA · PARTIALS</legend>
          <div className="knobs">
            <ParamKnob id={`${L}laMix`} label="WT↔DCO" size="sm" /><ParamKnob id={`${L}laRing`} label="RING" size="sm" />
            <ParamKnob id={`${L}wtMorph`} label="WT MORPH" size="sm" />
          </div>
          <p className="hint" title="Parcial A = wavetable · parcial B = SAW/PULSE del mixer. Añade un ataque en ATTACK TRANSIENT (pestaña EXPRESIÓN).">A = wavetable · B = SAW/PULSE</p>
        </fieldset>
      )}
      <fieldset className="group mixer-card"><legend>MIXER · OUT</legend>
        <div className="knobs">
          <ParamKnob id={`${L}saw`} label="SAW" size="sm" /><ParamKnob id={`${L}pulse`} label="PULSE" size="sm" />
          {type !== 4 && type !== 7 && <><ParamKnob id={`${L}sub`} label="SUB" size="sm" /><ParamKnob id={`${L}noise`} label="NOISE" size="sm" /></>}
          <ParamKnob id={`${L}pw`} label="PW" size="sm" /><ParamKnob id={`${L}pwmDepth`} label="PWM" size="sm" />
          <ParamKnob id={`${L}pwmRate`} label="PWM RATE" size="sm" /><ParamKnob id={`${L}drive`} label="DRIVE" size="sm" />
          <span className="knobs__sep" aria-hidden="true" />
          <OutKnobs L={L} />
        </div>
      </fieldset>
      {type !== 4 && (
        <fieldset className="group osc2-card"><legend>OSC 2 · FM · WT</legend>
          <ParamSegmented id={`${L}osc2Wave`} label="Onda OSC 2" options={SYNTH_OSC2_WAVES} />
          <div className="knobs">
            <ParamKnob id={`${L}osc2`} label="OSC2" size="sm" /><ParamKnob id={`${L}osc2Det`} label="DETUNE" size="sm" />
            <ParamKnob id={`${L}osc2Semi`} label="SEMI" size="sm" /><ParamKnob id={`${L}fmIndex`} label="FM INDEX" size="sm" />
            <ParamKnob id={`${L}fmRatio`} label="FM RATIO" size="sm" /><ParamKnob id={`${L}wtMorph`} label="WT MORPH" size="sm" />
          </div>
        </fieldset>
      )}
    </div>
  );
}

function Filter({ L }: { L: string }) {
  return (
    <>
      <fieldset className="group filter-card"><legend>FILTER</legend>
        <FilterScreen L={L} />
        <ParamSegmented id={`${L}filtType`} label="Tipo de filtro" options={SYNTH_FILTER_TYPES} />
        <div className="knobs">
          <ParamKnob id={`${L}cut`} label="CUTOFF" /><ParamKnob id={`${L}res`} label="RESO" size="sm" />
          <ParamKnob id={`${L}hpf`} label="HPF" size="sm" /><ParamKnob id={`${L}filtKT`} label="KEY TRACK" size="sm" /><ParamKnob id={`${L}velCut`} label="VEL→CUT" size="sm" />
        </div>
      </fieldset>
      <fieldset className="group env-card"><legend>FILTER ENVELOPE</legend>
        <EnvGraph ids={[`${L}fAtk`, `${L}fDec`, `${L}fSus`, `${L}fRel`]} label="Envolvente de filtro" />
        <div className="knobs">
          <ParamKnob id={`${L}filtEnv`} label="AMOUNT" size="sm" /><ParamKnob id={`${L}fAtk`} label="ATTACK" size="sm" /><ParamKnob id={`${L}fDec`} label="DECAY" size="sm" />
          <ParamKnob id={`${L}fSus`} label="SUSTAIN" size="sm" /><ParamKnob id={`${L}fRel`} label="RELEASE" size="sm" />
        </div>
      </fieldset>
    </>
  );
}

function AmpEnv({ L }: { L: string }) {
  return (
    <fieldset className="group env-card"><legend>AMP ENVELOPE</legend>
      <EnvGraph ids={[`${L}atk`, `${L}dec`, `${L}sus`, `${L}rel`]} label="Envolvente de amplitud" />
      <div className="knobs">
        <ParamKnob id={`${L}atk`} label="ATTACK" size="sm" /><ParamKnob id={`${L}dec`} label="DECAY" size="sm" />
        <ParamKnob id={`${L}sus`} label="SUSTAIN" size="sm" /><ParamKnob id={`${L}rel`} label="RELEASE" size="sm" />
        <ParamKnob id={`${L}velAmp`} label="VEL→AMP" size="sm" />
      </div>
    </fieldset>
  );
}

/** Mod wheel routes, attack transient and the remaining dynamics. */
function Expression({ L }: { L: string }) {
  return (
    <div className="deck-row">
      <fieldset className="group"><legend>MOD WHEEL</legend>
        <ParamSegmented id={`${L}wheelMode`} label="Destino de la rueda" options={SYNTH_WHEEL_MODES} />
        <div className="knobs">
          <ParamKnob id={`${L}wheelCutoff`} label="CUTOFF" size="sm" /><ParamKnob id={`${L}wheelAmp`} label="AMP" size="sm" />
          <ParamKnob id={`${L}wheelVibrato`} label="VIBRATO" size="sm" /><ParamKnob id={`${L}wheelVibRate`} label="VIB RATE" size="sm" />
          <ParamKnob id={`${L}wheelWtMorph`} label="WT MORPH" size="sm" />
        </div>
      </fieldset>
      <fieldset className="group"><legend>ATTACK TRANSIENT</legend>
        <ParamSelect id={`${L}trType`} label="TIPO" options={SYNTH_TRANSIENTS} />
        <div className="knobs">
          <ParamKnob id={`${L}trLevel`} label="LEVEL" size="sm" /><ParamKnob id={`${L}trDecay`} label="DECAY" size="sm" />
          <ParamKnob id={`${L}trBright`} label="BRIGHT" size="sm" />
        </div>
      </fieldset>
      <fieldset className="group"><legend>DYNAMICS</legend>
        <div className="knobs"><ParamKnob id={`${L}keyTrk`} label="KEY TRACK" size="sm" /><ParamKnob id={`${L}phase`} label="PHASE" size="sm" /></div>
      </fieldset>
    </div>
  );
}

function LfoLed({ n }: { n: number }) {
  const on = (useParam(`L${n}_lfoEnabled`).value ?? 0) >= .5;
  return <span className={`lfo-deck__led${on ? ' is-on' : ''}`} aria-label={on ? 'encendido' : 'apagado'} />;
}

/** LFO A-D: each belongs to its layer (direct routes) and is also a draggable mod source. */
function LfoDeck({ layer }: { layer: number }) {
  const [n, setN] = useState(layer + 1);
  const P = `L${n}_`;
  return (
    <div className="lfo-deck">
      <div className="lfo-deck__tabs" role="tablist" aria-label="LFO">
        {[1, 2, 3, 4].map(i => (
          <button key={i} type="button" role="tab" aria-selected={n === i} className={`lfo-deck__tab${n === i ? ' is-active' : ''}${i === layer + 1 ? ' is-layer' : ''}`}
            onClick={() => setN(i)} title={i === layer + 1 ? 'LFO de la capa que editas' : `LFO de la capa ${'ABCD'[i - 1]}`}>
            <LfoLed n={i} />LFO {'ABCD'[i - 1]}
          </button>
        ))}
      </div>
      <LfoScreen n={n} />
      <div className="lfo-deck__controls">
        <Row wrap><ParamToggle id={`${P}lfoEnabled`} label="ON" small /><ParamSegmented id={`${P}lfoWave`} label="Onda LFO" options={SYNTH_LFO_WAVES} /><ParamToggle id={`${P}lfoSync`} label="KEY SYNC" small /></Row>
        <div className="knobs">
          <ParamKnob id={`${P}lfoRate`} label="RATE" size="sm" /><ParamKnob id={`${P}lfoDepth`} label="DEPTH" size="sm" />
          <ParamKnob id={`${P}lfoDelay`} label="DELAY" size="sm" /><ParamKnob id={`${P}lfoFade`} label="FADE IN" size="sm" />
          <span className="lfo-deck__sep" aria-hidden="true" />
          <ParamKnob id={`${P}lfoPitch`} label="→PITCH" size="sm" /><ParamKnob id={`${P}lfoCutoff`} label="→CUTOFF" size="sm" />
          <ParamKnob id={`${P}lfoVolume`} label="→AMP" size="sm" /><ParamKnob id={`${P}lfoPan`} label="→PAN" size="sm" />
        </div>
        <p className="hint">LFO {'ABCD'[n - 1]} pertenece a la capa {'ABCD'[n - 1]}: sus destinos →PITCH / →CUTOFF / →AMP / →PAN actúan solo sobre esa capa.
          Arrastrando el chip «LFO {'ABCD'[n - 1]}» del riel puede modular cualquier knob de cualquier capa.</p>
      </div>
    </div>
  );
}

// The engine applies either the global voice settings (VOICE MASTER on, shared by
// layers A-D) or each layer's own; edit the set that is actually in effect.
function VoiceModeGroup({ L }: { L: string }) {
  const { value: master } = useParam('synthVoiceMaster');
  const global = (master ?? 1) >= .5;
  const id = (layerId: string, globalId: string) => (global ? globalId : `${L}${layerId}`);
  const layerName = SYNTH_LAYER_NAMES[Number(L.slice(1, 2)) - 1];
  return (
    <fieldset className="group"><legend>MODE · GLIDE · {global ? 'GLOBAL (A–D)' : `CAPA ${layerName}`}</legend>
      <ParamSegmented id="synthVoiceMaster" label="Ámbito de modo y glide" options={['POR CAPA', 'GLOBAL']} />
      <ParamSegmented id={id('voiceMode', 'synthVoiceMode')} label="Modo de voz" options={SYNTH_VOICE_MODES} />
      <Row wrap>
        <ParamToggle id={id('portamentoOn', 'synthPortamentoOn')} label="GLIDE" small />
        <ParamSegmented id={id('glideMode', 'synthGlideMode')} label="Modo glide" options={SYNTH_GLIDE_MODES} />
      </Row>
      <div className="knobs">
        <ParamKnob id={id('portamento', 'synthPortamento')} label="TIME" size="sm" /><ParamKnob id={id('slide', 'synthSlide')} label="SLIDE" size="sm" />
      </div>
    </fieldset>
  );
}

function Voice({ L }: { L: string }) {
  return (
    <div className="editor-grid editor-grid--3">
      <fieldset className="group"><legend>PITCH</legend>
        <div className="knobs"><ParamKnob id={`${L}oct`} label="OCTAVE" size="sm" /><ParamKnob id={`${L}semi`} label="SEMI" size="sm" /><ParamKnob id={`${L}det`} label="FINE" size="sm" /><ParamKnob id={`${L}analog`} label="ANALOG" size="sm" /></div>
      </fieldset>
      <fieldset className="group"><legend>UNISON</legend>
        <div className="knobs"><ParamKnob id={`${L}unison`} label="VOICES" size="sm" /><ParamKnob id={`${L}uniDet`} label="DETUNE" size="sm" /><ParamKnob id={`${L}uniSprd`} label="SPREAD" size="sm" /></div>
      </fieldset>
      <VoiceModeGroup L={L} />
    </div>
  );
}

const SEQ_DIVISIONS = ['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32'];

/** One sequencer step: playing LED, note offset, ACCENT / SLIDE (303 style) and REST. */
function SeqStep({ P, index, length, playing, readOnly }: { P: string; index: number; length: number; playing: boolean; readOnly: boolean }) {
  const note = useParam(`${P}seqStep${index}`);
  const acc = useParam(`${P}seqAcc${index}`);
  const slide = useParam(`${P}seqSlide${index}`);
  const v = Math.round(note.value ?? 99);
  const rest = v === 99;
  const accOn = (acc.value ?? 0) >= .5, slideOn = (slide.value ?? 0) >= .5;
  const off = index >= length;
  const cls = `seq-step${rest ? ' is-rest' : ''}${off ? ' is-off' : ''}${playing ? ' is-playing' : ''}${accOn ? ' is-accent' : ''}`;
  return (
    <div className={cls} aria-label={`Paso ${index + 1}`}>
      <span className="seq-step__head"><span className={`led${playing ? ' is-on' : ''}`} aria-hidden="true" />{index + 1}</span>
      <button type="button" className="icon-btn" disabled={readOnly} aria-label={`Paso ${index + 1} +1`} onClick={() => note.set(rest ? 0 : Math.min(24, v + 1))}>▲</button>
      <output className="seq-step__value">{rest ? '—' : v > 0 ? `+${v}` : v}</output>
      <button type="button" className="icon-btn" disabled={readOnly} aria-label={`Paso ${index + 1} −1`} onClick={() => note.set(rest ? 0 : Math.max(-24, v - 1))}>▼</button>
      <button type="button" className={`seq-step__flag${accOn ? ' is-on' : ''}`} disabled={readOnly} aria-pressed={accOn} title="ACCENT: paso más fuerte" onClick={() => acc.set(accOn ? 0 : 1)}>ACC</button>
      <button type="button" className={`seq-step__flag${slideOn ? ' is-on' : ''}`} disabled={readOnly} aria-pressed={slideOn} title="SLIDE: liga y desliza al paso siguiente" onClick={() => slide.set(slideOn ? 0 : 1)}>SLD</button>
      <button type="button" className="seq-step__rest" disabled={readOnly} aria-pressed={rest} onClick={() => note.set(rest ? 0 : 99)}>{rest ? 'ON' : 'REST'}</button>
    </div>
  );
}

function Seq({ L }: { L: string }) {
  const layer = Number(L.slice(1, -1)) - 1;
  const shared = (useParam('synthSeqShare').value ?? 0) >= .5;
  const clock = (useParam('synthSeqClock').value ?? 0) >= .5;
  const layerSync = (useParam(`${L}seqSync`).value ?? 0) >= .5;   // hooks never behind || / &&
  const synced = clock || layerSync;
  const P = shared ? 'L1_' : L;                              // pattern source
  const readOnly = shared && layer > 0;
  const length = Math.round(useParam(`${P}seqSteps`).value ?? 16);
  const step = useEngine(s => (s as { synthSeqStep?: number[] } | undefined)?.synthSeqStep?.[layer] ?? -1);
  return (
    <div className="seq">
      <div className="seq__transport">
        <ParamToggle id={`${L}seqOn`} label="SEQUENCER" small />
        <ParamToggle id={`${L}seqSync`} label="SYNC" small />
        <ParamSegmented id={`${L}seqDiv`} label="División" options={SEQ_DIVISIONS} />
        <div className={`seq__bpm${synced ? ' is-synced' : ''}`} title={synced ? 'Sigue el tempo master' : 'Tempo propio de la capa'}>
          <ParamKnob id={`${L}seqBpm`} label={synced ? 'BPM · MASTER' : 'BPM'} size="sm" />
        </div>
        <ParamKnob id={`${P}seqSteps`} label="STEPS" size="sm" />
        <ParamKnob id={`${P}seqGate`} label="GATE" size="sm" />
        <div className="seq__global" role="group" aria-label="Secuenciadores: global">
          <ParamToggle id="synthSeqClock" label="SYNC CLOCK" small />
          <ParamToggle id="synthSeqShare" label="SHARE PATTERN" small />
        </div>
      </div>
      <p className="hint">
        {clock ? 'SYNC CLOCK: las 4 capas siguen el tempo master y arrancan juntas. ' : ''}
        {shared ? (layer > 0 ? 'SHARE PATTERN: esta capa toca el patrón de la capa A (edítalo en A). ' : 'SHARE PATTERN: las capas B–D tocan este patrón. ') : ''}
        ACC = acento · SLD = liga y desliza al paso siguiente (estilo 303).
      </p>
      <div className={`seq__steps${readOnly ? ' is-readonly' : ''}`}>
        {Array.from({ length: 16 }, (_, i) => <SeqStep key={`${P}${i}`} P={P} index={i} length={length} playing={step === i} readOnly={readOnly} />)}
      </div>
    </div>
  );
}

function Inserts({ L }: { L: string }) {
  return (
    <div className="synth-inserts">
      <SynthEq L={L} />
      <InsertChain unitPrefix={u => `${L}fx${u}_`} />
    </div>
  );
}

/** Layer clipboard (COPY / PASTE between layers, or between presets). */
let layerClipboard: unknown | null = null;

function LayerTools({ layer }: { layer: number }) {
  const config = useEngine(s => s?.params?.synthLayers?.[layer]);
  const [hasClip, setHasClip] = useState(layerClipboard !== null);
  const name = SYNTH_LAYER_NAMES[layer];
  return (
    <div className="layer-tools" role="group" aria-label="Herramientas de capa">
      <Btn small onClick={() => void runAction(() => api.synthLayerApply(layer, null), `Capa ${name} inicializada`)} title="Restablece la capa a un oscilador básico">INIT</Btn>
      <Btn small onClick={() => { layerClipboard = config ? JSON.parse(JSON.stringify(config)) : null; setHasClip(layerClipboard !== null); }} title="Copia toda la capa">COPY</Btn>
      <Btn small disabled={!hasClip} onClick={() => void runAction(() => api.synthLayerApply(layer, layerClipboard), `Pegado en la capa ${name}`)} title="Pega la capa copiada">PASTE</Btn>
    </div>
  );
}

/** Layer tabs A-D: LED, engine, key range and level at a glance. */
function LayerTab({ index, active, onSelect }: { index: number; active: boolean; onSelect: () => void }) {
  const cfg = useEngine(s => s?.params?.synthLayers?.[index]) as { synthesisType?: number; keyLow?: number; keyHigh?: number; volume?: number; outGainDb?: number; dx7Voice?: string; mt32Program?: number } | undefined;
  const gain = Math.round(cfg?.outGainDb ?? 0);
  const type = Math.round(cfg?.synthesisType ?? 0);
  const level = Math.min(1, (cfg?.volume ?? 0) / 1.5);
  let engine = SYNTH_SYNTHESIS_TYPES[type] ?? '';
  if (type === 5 && cfg?.dx7Voice && cfg.dx7Voice.length >= 310)
    engine = 'DX7 · ' + Array.from({ length: 10 }, (_, i) => String.fromCharCode(parseInt(cfg.dx7Voice!.slice((145 + i) * 2, (146 + i) * 2), 16))).join('').trim();
  if (type === 6) engine = `MT-32 · ${(cfg?.mt32Program ?? 0) < 64 ? 'A' : 'B'}${String(((cfg?.mt32Program ?? 0) % 64) + 1).padStart(2, '0')}`;
  return (
    <div className={`layer-tab${active ? ' is-active' : ''}`}>
      <ParamLed id={`L${index + 1}_en`} label={`Capa ${SYNTH_LAYER_NAMES[index]}`} />
      <button type="button" role="tab" aria-selected={active} className="layer-tab__main" onClick={onSelect}>
        <span className="layer-tab__letter">{SYNTH_LAYER_NAMES[index]}</span>
        <span className="layer-tab__info">
          <span className="layer-tab__engine">{engine}</span>
          {gain !== 0 && <span className="layer-tab__gain" title="GAIN de salida de la capa">{gain > 0 ? '+' : ''}{gain} dB</span>}
          <span className="layer-tab__range">{noteName(cfg?.keyLow ?? 0)}–{noteName(cfg?.keyHigh ?? 127)}</span>
        </span>
        <span className="layer-tab__level" title={`LEVEL ${Math.round(level * 150)} % · medidor: salida de la capa`}><SourceMeter source={5 + index} /></span>
      </button>
    </div>
  );
}

function SynthWorkspace({ layer, setLayer }: { layer: number; setLayer: (l: number) => void }) {
  const [deck, setDeck] = useState(0);
  const [expanded, setExpanded] = useState(false);
  const L = `L${layer + 1}_`;
  return (
    <div className="synth-workspace">
      <div className="layer-tabs" role="tablist" aria-label="Capas del sintetizador">
        {Array.from({ length: SYNTH_LAYER_COUNT }, (_, i) => <LayerTab key={i} index={i} active={layer === i} onSelect={() => setLayer(i)} />)}
        <LayerTools layer={layer} />
      </div>
      <ModRail layer={layer} />
      <div className={`synth-editor${expanded ? ' is-deck-expanded' : ''}`}>
        <div className="synth-editor__row synth-editor__row--engine"><Osc L={L} /></div>
        <div className="synth-editor__row synth-editor__row--shape">
          <Filter L={L} />
          <AmpEnv L={L} />
        </div>
        <div className="synth-deck">
          <div className="tabs synth-deck__tabs" role="tablist">
            {DECK_TABS.map((t, i) => (
              <button key={t} type="button" role="tab" aria-selected={deck === i} className={`tabs__tab${deck === i ? ' is-active' : ''}`} onClick={() => setDeck(i)}>{t}</button>
            ))}
            <button type="button" className={`synth-deck__expand${expanded ? ' is-on' : ''}`} aria-pressed={expanded} onClick={() => setExpanded(e => !e)}
              title={expanded ? 'Volver a mostrar FILTER y ENVELOPES' : 'Ampliar este panel sobre FILTER y ENVELOPES'}>{expanded ? '⤡ REDUCIR' : '⤢ AMPLIAR'}</button>
          </div>
          <div className="synth-deck__body" role="tabpanel" key={`${deck}-${layer}`}>
            {deck === 0 && <LfoDeck layer={layer} />}
            {deck === 1 && <ModMatrixPanel layer={layer} />}
            {deck === 2 && <Expression L={L} />}
            {deck === 3 && <Voice L={L} />}
            {deck === 4 && <Seq L={L} />}
            {deck === 5 && <Inserts L={L} />}
          </div>
        </div>
      </div>
    </div>
  );
}

export default function SynthView() {
  const [layer, setLayer] = useState(0);
  return (
    <ModuleTabs module={1}>
      <div className="view view--synth view--synth-v324">
        <PatchBar layer={layer} />
        <SynthWorkspace layer={layer} setLayer={setLayer} />
      </div>
    </ModuleTabs>
  );
}
