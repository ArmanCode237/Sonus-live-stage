// Parameter ID (written with POST /api/param, as registered in
// WorkstationHost::registerAllParameters) → current value inside the
// serializeParams() snapshot returned by GET /api/state.
//
// This is the single source of truth that lets every control be declared by ID:
// it reads its value from here and writes through the parameter writer.
import type { EngineParams } from '../api/types';

type Obj = Record<string, unknown>;
const num = (v: unknown): number | undefined =>
  typeof v === 'number' ? v : typeof v === 'boolean' ? (v ? 1 : 0) : undefined;
const at = (o: unknown, ...path: (string | number)[]): unknown => {
  let cur: unknown = o;
  for (const key of path) {
    if (cur === null || typeof cur !== 'object') return undefined;
    cur = (cur as Obj)[key as string];
  }
  return cur;
};

/** IDs whose state key differs from the ID (or is nested). Same-name IDs fall through. */
const DIRECT: Record<string, string> = {
  tune: 'masterTuneCents', cutoff: 'cutoffHz', resonance: 'resonanceQ',
  attack: 'attackSec', decay: 'decaySec', sustain: 'sustainLevel', release: 'releaseSec',
  lowCut: 'lowCutHz', highCut: 'highCutHz',
  chorusRate: 'chorusRateHz', phaserRate: 'phaserRateHz', tremoloRate: 'tremoloRateHz',
  transpose: 'transposeSemis', octave: 'octaveShift', touch: 'kbdTouch',
  masterDriveOn: 'masterDriveEnabled', masterAutoPanOn: 'masterAutoPanEnabled',
  synthPortamentoOn: 'synthPortamentoEnabled',
  synthSeqClock: 'synthSeqGlobalClock', synthSeqShare: 'synthSeqSharePattern',
};

const SAMPLE_FIELDS: Record<string, string> = {
  Atk: 'attackSec', Dec: 'decaySec', Sus: 'sustainLevel', Rel: 'releaseSec', Hold: 'loopHold',
  Pan: 'pan', Tune: 'fineTuneCents', Cut: 'cutoffHz', Res: 'resonanceQ', Gain: 'gainDb',
  WheelCutoff: 'wheelCutoffOctaves', WheelAmp: 'wheelAmpDb', PortOn: 'portamentoEnabled',
  PortTime: 'portamentoSec', PortMode: 'portamentoMode', Mono: 'monoMode', Legato: 'legatoEnabled',
  WheelCutoffOn: 'wheelCutoffEnabled', WheelCutoffInvert: 'wheelCutoffInvert', WheelAmpOn: 'wheelAmpEnabled',
  WheelVibratoOn: 'wheelVibratoEnabled', WheelVibratoDepth: 'wheelVibratoCents', WheelVibratoRate: 'wheelVibratoRateHz',
  EqHP: 'soundEqHighPass', EqLP: 'soundEqLowPass',
  Eq0: 'soundEqEnabled', Eq1: 'soundEqHighPass', Eq2: 'soundEqLowPass', Eq3: 'soundEqHighPassSlope', Eq4: 'soundEqLowPassSlope',
};
const SAMPLE_BAND: Record<string, string> = { EqF: 'soundEqFreq', EqQ: 'soundEqQ', EqG: 'soundEqGain' };

const LAYER_FIELDS: Record<string, string> = {
  en: 'enabled', wave: 'waveType', vol: 'volume', pan: 'pan', oct: 'octave', semi: 'semitones', det: 'detuneCents',
  cut: 'cutoffHz', res: 'resonanceQ', atk: 'attackSec', dec: 'decaySec', sus: 'sustainLevel', rel: 'releaseSec',
  unison: 'unisonVoices', uniDet: 'unisonDetune', uniSprd: 'unisonSpread', phase: 'phaseOffset', keyTrk: 'keyTrack',
  velCut: 'velToCutoff', velAmp: 'velToAmp', wtIdx: 'wavetableIdx', wtMorph: 'wavetableMorph',
  filtType: 'filterType', filtEnv: 'filterEnvAmt', filtKT: 'filterKeyTrack',
  fAtk: 'filtAttackSec', fDec: 'filtDecaySec', fSus: 'filtSustainLvl', fRel: 'filtReleaseSec',
  saw: 'sawLevel', pulse: 'pulseLevel', pw: 'pulseWidth', pwmDepth: 'pwmDepth', pwmRate: 'pwmRateHz',
  sub: 'subLevel', noise: 'noiseLevel', osc2: 'osc2Level', osc2Det: 'osc2Detune', osc2Semi: 'osc2Semitones',
  osc2Wave: 'osc2Wave', hpf: 'highpassHz', drive: 'drive', synthType: 'synthesisType', fmIndex: 'fmIndex',
  fmRatio: 'fmRatio', wheelMode: 'wheelMode', wheelCutoff: 'wheelCutoffOctaves', wheelAmp: 'wheelAmpDb',
  wheelVibrato: 'wheelVibratoCents', wheelVibRate: 'wheelVibratoRateHz', wheelWtMorph: 'wheelWtMorph',
  voiceMode: 'voiceMode', portamentoOn: 'portamentoEnabled', portamento: 'portamento', glideMode: 'glideMode',
  slide: 'slide', keyLow: 'keyLow', keyHigh: 'keyHigh', keyTrans: 'keyTranspose',
  seqOn: 'seqEnabled', seqBpm: 'seqBpm', seqSteps: 'seqSteps', seqGate: 'seqGate', seqDiv: 'seqDivision', seqSync: 'seqSync', gain: 'outGainDb',
  eqOn: 'eqEnabled', eqHp: 'eqHighPass', eqLp: 'eqLowPass', eqHpSlope: 'eqHighPassSlope', eqLpSlope: 'eqLowPassSlope',
  analog: 'analog', fm4Algo: 'fm4Algorithm', fm4Fb: 'fm4Feedback', fm4Vel: 'fm4VelMod', laMix: 'laMix', laRing: 'laRing',
  trType: 'transientType', trLevel: 'transientLevel', trDecay: 'transientDecay', trBright: 'transientTone',
};
/** FM4 operator fields: L1_op2Ratio → synthLayers[0].opRatio[1]. */
const OP_FIELDS: Record<string, string> = { Ratio: 'opRatio', Level: 'opLevel', Atk: 'opAttack', Dec: 'opDecay', Sus: 'opSustain' };
const LFO_FIELDS: Record<string, string> = {
  lfoEnabled: 'enabled', lfoWave: 'waveType', lfoRate: 'rateHz', lfoDepth: 'depth', lfoPitch: 'destPitch',
  lfoCutoff: 'destCutoff', lfoVolume: 'destVolume', lfoPan: 'destPan',
  lfoDelay: 'delaySec', lfoFade: 'fadeSec', lfoSync: 'keySync',
};
const INSERT_FIELDS: Record<string, string> = {
  on: 'enabled', rate: 'rateHz', depth: 'depth', mix: 'mix', feedback: 'feedback',
  time: 'timeMs', sync: 'syncEnabled', division: 'syncDivision',
  type: 'type', damp: 'damping', predelay: 'preDelayMs',
};
const BUS: Record<string, string> = { Mod: 'modulation', Delay: 'delay', Reverb: 'reverb' };
const ZONE_EDGE: Record<string, string> = { Low: 'low', High: 'high', Oct: 'octave', Semi: 'transpose' };
/** Module FX fields: id suffix → synthFx / organFx key. */
const MODULE_FX: Record<string, string> = {
  ChorusOn: 'chorusEnabled', ChorusRate: 'chorusRate', ChorusDepth: 'chorusDepth', ChorusMix: 'chorusMix',
  DelayOn: 'delayEnabled', DelayTime: 'delayMs', DelayFeedback: 'delayFeedback', DelayMix: 'delayMix',
  ReverbOn: 'reverbEnabled', ReverbType: 'reverbType', ReverbMix: 'reverbMix', ReverbSize: 'reverbSize',
  DriveOn: 'driveEnabled', DriveAmount: 'driveAmount', AutoPanOn: 'autoPanEnabled', AutoPanRate: 'autoPanRate',
  AutoPanDepth: 'autoPanDepth', PhaserOn: 'phaserEnabled', PhaserRate: 'phaserRate', PhaserDepth: 'phaserDepth',
  PhaserFeedback: 'phaserFeedback', PhaserMix: 'phaserMix', TremoloOn: 'tremoloEnabled', TremoloRate: 'tremoloRate',
  TremoloDepth: 'tremoloDepth', FlangerOn: 'flangerEnabled', FlangerRate: 'flangerRate', FlangerDepth: 'flangerDepth',
  FlangerFeedback: 'flangerFeedback', FlangerMix: 'flangerMix',
};

/** Legacy macros that write one value into all four synth layers; layer A is the reference. */
const SYNTH_MACROS: Record<string, string> = {
  'synth-unison-voices': 'unisonVoices', 'synth-unison-detune': 'unisonDetune', 'synth-unison-spread': 'unisonSpread',
  'synth-phase': 'phaseOffset', 'synth-keytrack': 'keyTrack', 'synth-vel-cutoff': 'velToCutoff', 'synth-vel-amp': 'velToAmp',
  'synth-filt-envamt': 'filterEnvAmt', 'synth-filt-atk': 'filtAttackSec', 'synth-filt-dec': 'filtDecaySec',
  'synth-filt-sus': 'filtSustainLvl', 'synth-filt-rel': 'filtReleaseSec',
};

type Rule = [RegExp, (p: EngineParams, m: RegExpMatchArray) => unknown];
const RULES: Rule[] = [
  [/^synth-[\w-]+$/, (p, m) => at(p, 'synthLayers', 0, SYNTH_MACROS[m[0]] ?? '')],
  [/^L([1-4])_fx([0-5])_(\w+)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, 'insertFx', +m[2], INSERT_FIELDS[m[3]] ?? '')],
  [/^L([1-4])_seqStep(\d+)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, 'seqNotes', +m[2])],
  [/^synthMacro([1-4])$/, (p, m) => at(p, 'synthMacros', +m[1] - 1)],
  [/^L([1-4])_eq([FQG])([0-2])$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, ({ F: 'eqFreq', Q: 'eqQ', G: 'eqGain' } as Record<string, string>)[m[2]], +m[3])],
  [/^L([1-4])_seqAcc(\d+)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, 'seqAccent', +m[2])],
  [/^L([1-4])_seqSlide(\d+)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, 'seqSlide', +m[2])],
  [/^L([1-4])_op([1-4])(Ratio|Level|Atk|Dec|Sus)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, OP_FIELDS[m[3]], +m[2] - 1)],
  [/^L([1-4])_(lfo\w+)$/, (p, m) => at(p, 'synthLfos', +m[1] - 1, LFO_FIELDS[m[2]] ?? '')],
  [/^L([1-4])_(\w+)$/, (p, m) => at(p, 'synthLayers', +m[1] - 1, LAYER_FIELDS[m[2]] ?? '')],
  // v31: a piano layer Sound's own inserts live in its sample config.
  [/^pianoFx([0-4])_([0-5])_(\w+)$/, (p, m) => at(p, 'pianoSampleConfig', +m[1], 'insertFx', +m[2], INSERT_FIELDS[m[3]] ?? '')],
  [/^pianoSample([0-4])(EqF|EqQ|EqG)([0-2])$/, (p, m) => at(p, 'pianoSampleConfig', +m[1], SAMPLE_BAND[m[2]], +m[3])],
  [/^pianoSample([0-4])(\w+)$/, (p, m) => at(p, 'pianoSampleConfig', +m[1], SAMPLE_FIELDS[m[2]] ?? '')],
  [/^pianoLayerVol([0-4])$/, (p, m) => at(p, 'pianoLayers', +m[1], 'volume')],
  [/^pianoLayerEnabled([0-4])$/, (p, m) => at(p, 'pianoLayers', +m[1], 'enabled')],
  [/^pianoZone([0-4])(Low|High|Oct|Semi)$/, (p, m) => at(p, 'pianoLayerZones', +m[1], ZONE_EDGE[m[2]])],
  [/^(piano|synth|organ)Zone(Low|High)$/, (p, m) => at(p, `${m[1]}Zone`, ZONE_EDGE[m[2]])],
  [/^eqGain([0-4])$/, (p, m) => at(p, 'eqBands', +m[1], 'gainDb')],
  [/^eqFreq([0-4])$/, (p, m) => at(p, 'eqBands', +m[1], 'freqHz')],
  [/^eqQ([0-4])$/, (p, m) => at(p, 'eqBands', +m[1], 'q')],
  [/^drawbar([0-8])$/, (p, m) => at(p, 'drawbars', +m[1])],
  [/^fxSend(\d)(Mod|Delay|Reverb)$/, (p, m) => at(p, 'fxSends', +m[1], BUS[m[2]])],
  [/^synthSend(\d)(Mod|Delay|Reverb)$/, (p, m) => at(p, 'synthFxSends', +m[1], BUS[m[2]])],
  [/^organSend(Mod|Delay|Reverb)$/, (p, m) => at(p, 'organFxSends', BUS[m[1]])],
  [/^(synth|organ)Fx(\w+)$/, (p, m) => at(p, `${m[1]}Fx`, MODULE_FX[m[2]] ?? '')],
  [/^studioSlot(\d)_(\d)_p(\d+)$/, (p, m) => at(p, 'studioRacks', +m[1], 'slots', +m[2], 'params', +m[3])],
  [/^studioSend(\d)_(\d)_(\d)$/, (p, m) => at(p, 'studioRacks', +m[1], 'sends', +m[2], +m[3])],
];

const cache = new Map<string, (p: EngineParams) => unknown>();
function resolver(id: string): (p: EngineParams) => unknown {
  let fn = cache.get(id);
  if (fn) return fn;
  if (id in DIRECT) { const key = DIRECT[id]; fn = p => p[key]; }
  else {
    for (const [re, read] of RULES) {
      const m = id.match(re);
      if (m) { fn = p => read(p, m); break; }
    }
    if (!fn) fn = p => p[id];
  }
  cache.set(id, fn);
  return fn;
}

/** Current engine value for a parameter ID (booleans as 0/1), or undefined if unknown. */
export function readParam(params: EngineParams | undefined, id: string): number | undefined {
  if (!params) return undefined;
  return num(resolver(id)(params));
}
