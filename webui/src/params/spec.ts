// Display metadata for parameters: unit, response curve and readout text.
// Ranges come from the engine catalogue (GET /api/control → parameters).

export type Unit = 'Hz' | 's' | 'ms' | '%' | 'dB' | 'st' | 'ct' | 'pan' | 'oct' | 'int' | 'bool' | 'x' | 'bpm' | 'raw';

export interface ParamSpec {
  id: string;
  min: number;
  max: number;
  unit: Unit;
  /** Logarithmic knob response (frequencies and times). */
  log: boolean;
  /** Integer steps (modes, semitones, voices). */
  step?: number;
  /** Value restored on double-click. */
  defaultValue: number;
}

const UNIT_RULES: [RegExp, Unit][] = [
  [/(Enabled|On|^en$|_en$|_on$|Mono|Legato|Invert|Sync$|_sync$|PingPong|TempoSync|softRelease|Fast$|VoiceMaster|_seqOn$)/, 'bool'],
  [/(Bpm|bpm)$/, 'bpm'],
  [/(Pan|pan|_pan|lfoPan)$/, 'pan'],
  [/(Tune|tune|Cents|_det|osc2Det|Detune|uniDet|wheelVibrato$|WheelVibratoDepth)$/, 'ct'],
  [/(semi|Semi|Trans|transpose|ShiftSt|slide|Slide|keyTrans)$/, 'st'],
  [/(oct|octave|WheelCutoff|wheelCutoff|synthWheelGlobalCutoff)$/, 'oct'],
  [/(Db|dB|Gain\d?$|EqG\d|WheelAmp$|wheelAmp$|GlobalAmpDb|Gain$)/, 'dB'],
  [/(Hz|Freq\d?$|cutoff|Cut$|_cut$|lowCut|highCut|hpf|EqHP|EqLP|EqF\d|Eq1$|Eq2$|Tone$|Rate\d?$|rate$|_rate|RateHz|VibRate|pwmRate|lfoRate|chorusTone|VibratoRate)/i, 'Hz'],
  [/(Ms|_time$|Time$|DelayTime|delayTimeMs|PreDelay|_predelay)$/, 'ms'],
  [/(Sec$|attack|decay|release|Atk$|Dec$|Rel$|trDecay|lfoDelay|lfoFade|_atk$|_dec$|_rel$|fAtk|fDec|fRel|PortTime|portamento$|Portamento$)/, 's'],
  [/(Res$|_res$|resonance|EqQ\d|^eqQ\d|fmRatio|fmIndex|op\dRatio|Q$)/, 'x'],
  [/(Type|Mode|Wave|wave|Model|Algo|Harmonic|Division|division|Voices|voices|unison|wtIdx|Steps|maxVoices|touch|Low$|High$|keyLow|keyHigh|seqStep\d+|lfoPitch|lfoCutoff|synthLfoPitch|synthLfoCutoff|synthPresetId)$/, 'int'],
];
const LINEAR_HZ = /(lfoRate|Rate|rate|VibRate|pwmRate|VibratoRate)/;

export function inferUnit(id: string): Unit {
  for (const [re, unit] of UNIT_RULES) if (re.test(id)) return unit;
  return '%';
}

export function makeSpec(id: string, min: number, max: number): ParamSpec {
  const unit = inferUnit(id);
  const positive = min > 0;
  const log = positive && max / min >= 40 && (unit === 'Hz' ? !LINEAR_HZ.test(id) : unit === 's' || unit === 'ms');
  const step = unit === 'int' || unit === 'bool' || unit === 'st' && /semi|Semi|Trans|transpose|keyTrans/.test(id) || unit === 'oct' && /oct$|octave$/.test(id) ? 1 : undefined;
  const defaultValue = unit === 'pan' || (min < 0 && max > 0) ? 0 : min;
  return { id, min, max, unit, log, step, defaultValue };
}

/** 0..1 knob position ↔ engine value. */
export function toNorm(spec: ParamSpec, value: number): number {
  const v = Math.min(spec.max, Math.max(spec.min, value));
  if (spec.max === spec.min) return 0;
  return spec.log ? Math.log(v / spec.min) / Math.log(spec.max / spec.min) : (v - spec.min) / (spec.max - spec.min);
}
export function fromNorm(spec: ParamSpec, norm: number): number {
  const n = Math.min(1, Math.max(0, norm));
  let v = spec.log ? spec.min * (spec.max / spec.min) ** n : spec.min + n * (spec.max - spec.min);
  if (spec.step) v = Math.round(v / spec.step) * spec.step;
  return Math.min(spec.max, Math.max(spec.min, v));
}

const trim = (n: number, digits: number) => Number(n.toFixed(digits)).toString();

export function formatValue(spec: ParamSpec, value: number | undefined): string {
  if (value === undefined || Number.isNaN(value)) return '—';
  switch (spec.unit) {
    case 'bool': return value >= 0.5 ? 'ON' : 'OFF';
    case 'Hz': return value >= 1000 ? `${trim(value / 1000, 2)} kHz` : value < 10 ? `${trim(value, 2)} Hz` : `${Math.round(value)} Hz`;
    case 's': return value < 1 ? `${Math.round(value * 1000)} ms` : `${trim(value, 2)} s`;
    case 'ms': return `${Math.round(value)} ms`;
    case 'dB': return `${value > 0 ? '+' : ''}${trim(value, 1)} dB`;
    case 'st': return `${value > 0 ? '+' : ''}${trim(value, 1)} st`;
    case 'ct': return `${value > 0 ? '+' : ''}${trim(value, 1)} ct`;
    case 'oct': return `${value > 0 ? '+' : ''}${trim(value, 2)} oct`;
    case 'pan': return Math.abs(value) < 0.02 ? 'C' : value < 0 ? `L${Math.round(-value * 100)}` : `R${Math.round(value * 100)}`;
    case 'int': return String(Math.round(value));
    case 'bpm': return `${Math.round(value)} BPM`;
    case 'x': return trim(value, 2);
    case 'raw': return trim(value, 3);
    default: {
      // Percent of the parameter range (levels 0..1.5 show 150 %).
      const pct = spec.max <= 2 && spec.min >= -2 ? value * 100 : ((value - spec.min) / (spec.max - spec.min)) * 100;
      return `${Math.round(pct)} %`;
    }
  }
}
