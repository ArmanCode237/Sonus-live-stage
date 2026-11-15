// Studio rack model: 4 racks (PIANO, SYNTH, ORGAN, MASTER) × EQ + 5 insert slots.
// Slot parameters are normalized 0..1. Field numbers follow the C++ /api/studio-racks
// contract: 0 = type, 1 = enabled, p0..p15 → 2..17, 18 = algorithm version,
// p16..p23 → 19..26. Display formulas are ported verbatim from the legacy UI.
import { NOTE_DIVISIONS, REVERB_TYPES, SHIMMER_COLOR_MODES, SHIMMER_PITCH_MODES, SHIMMER_SPATIAL_MODES } from './constants';

export const STUDIO_TYPES: readonly string[] = [
  'EMPTY', 'CHORUS', 'PHASER', 'TREMOLO', 'FLANGER', 'STEREO DELAY', 'REVERB', 'ANALOG DRIVE',
  'AUTO-PAN', 'COMPRESSOR', 'BITCRUSHER', 'STEREO WIDTH', 'PARAMETRIC EQ',
];
export const STUDIO_RACKS: readonly string[] = ['PIANO', 'SYNTH', 'ORGAN', 'MASTER'];
export const STUDIO_CHANNELS: readonly (readonly string[])[] = [
  ['PIANO L1', 'PIANO L2', 'PIANO L3', 'PIANO L4', 'PIANO L5'],
  ['SYNTH A', 'SYNTH B', 'SYNTH C', 'SYNTH D'],
  ['ORGAN'],
  ['PIANO BUS', 'SYNTH BUS', 'ORGAN BUS'],
];
export const STUDIO_EQ_BANDS: readonly string[] = ['LOW', 'LOW-MID', 'MID', 'HIGH-MID', 'HIGH'];
export const STUDIO_SLOTS = 5;

export const FIELD_TYPE = 0;
export const FIELD_ENABLED = 1;
export const FIELD_VERSION = 18;
export const slotField = (param: number): number => (param < 16 ? param + 2 : param + 3);

export type ControlDef = readonly [param: number, label: string, unit: string];

const CONTROLS: Record<number, readonly ControlDef[]> = {
  1: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [2, 'FEEDBACK', '%'], [3, 'MIX', '%'], [4, 'WIDTH', '%'], [5, 'TONE', 'Hz']],
  2: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [2, 'FEEDBACK', '%'], [3, 'MIX', '%']],
  3: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [3, 'MIX', '%']],
  4: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [2, 'FEEDBACK', '%'], [3, 'MIX', '%']],
  5: [[0, 'LEVEL', '%'], [2, 'FEEDBACK', '%'], [3, 'TIME', 'ms'], [4, 'PING-PONG', '%'], [5, 'DAMPING', '%']],
  6: [[0, 'LEVEL', '%'], [1, 'DAMPING', '%'], [2, 'SHIMMER', '%'], [3, 'PRE-DELAY', 'ms'], [4, 'SIZE', '%']],
  7: [[0, 'DRIVE', '%'], [3, 'MIX', '%']],
  8: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [3, 'MIX', '%']],
  9: [[0, 'THRESHOLD', 'dB'], [1, 'RATIO', ':1'], [2, 'MAKEUP', 'dB'], [3, 'ATTACK', 'ms'], [4, 'RELEASE', 'ms']],
  10: [[0, 'BITS', 'bits'], [1, 'DOWNSAMPLE', 'x'], [3, 'MIX', '%']],
  11: [[0, 'WIDTH', '%'], [3, 'MIX', '%']],
  12: [[0, 'WET', '%'], [1, 'HIGH PASS', 'Hz'], [2, 'LOW PASS', 'Hz'], [3, 'LOW GAIN', 'dB'], [4, 'MID GAIN', 'dB'],
    [5, 'HIGH GAIN', 'dB'], [6, 'LOW FREQ', 'Hz'], [7, 'MID FREQ', 'Hz'], [8, 'HIGH FREQ', 'Hz'], [9, 'LOW Q', 'Q'],
    [13, 'MID Q', 'Q'], [14, 'HIGH Q', 'Q'], [12, 'OUTPUT', 'dB']],
};
// Version ≥ 2 algorithms expose extra parameters.
const CONTROLS_ENHANCED: Record<number, readonly ControlDef[]> = {
  1: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [2, 'FEEDBACK', '%'], [3, 'MIX', '%'], [4, 'WIDTH', '%'], [5, 'TONE', 'Hz'], [6, 'VOICES', 'voices'], [8, 'BASE DELAY', 'ms']],
  2: [[0, 'RATE', 'Hz'], [1, 'DEPTH', '%'], [2, 'FEEDBACK', '%'], [3, 'MIX', '%'], [4, 'STAGES', 'stages'], [5, 'FREQ MIN', 'Hz'], [6, 'FREQ MAX', 'Hz'], [7, 'ST PHASE', '°']],
  7: [[0, 'DRIVE', '%'], [2, 'TONE', 'Hz'], [3, 'MIX', '%'], [4, 'OUTPUT', 'dB'], [5, 'BIAS', '%']],
  9: [[0, 'THRESHOLD', 'dB'], [1, 'RATIO', ':1'], [2, 'MAKEUP', 'dB'], [3, 'ATTACK', 'ms'], [4, 'RELEASE', 'ms'], [5, 'KNEE', 'dB'], [6, 'DET HPF', 'Hz'], [7, 'PARALLEL', '%']],
};
const REVERB_CONTROLS: readonly (readonly ControlDef[])[] = [
  [[0, 'WET', '%'], [1, 'WALL DAMP', '%'], [3, 'PRE-DELAY', 'ms'], [4, 'ROOM SIZE', '%']],
  [[0, 'WET', '%'], [1, 'HIGH DAMP', '%'], [3, 'PRE-DELAY', 'ms'], [4, 'DECAY', '%']],
  [[0, 'WET', '%'], [1, 'AIR DAMP', '%'], [3, 'PRE-DELAY', 'ms'], [4, 'HALL SIZE', '%']],
  [[0, 'WET', '%'], [1, 'DARKNESS', '%'], [2, 'PITCH RETURN', '%'], [3, 'PRE-DELAY', 'ms'], [4, 'SPACE', '%'],
    [6, 'PITCH A', 'st'], [7, 'DECAY', '%'], [8, 'LOW CUT', 'Hz'], [9, 'HIGH CUT', 'Hz'],
    [10, 'MOD RATE', 'Hz'], [11, 'MOD DEPTH', '%'], [15, 'DIFFUSION', '%']],
];

export interface StudioSlot { type: number; enabled: boolean; version: number; params: number[] }
export interface StudioRack {
  eqEnabled: boolean; eqDb: number[]; eqFreq: number[]; eqQ: number[];
  eqHighPass: number; eqLowPass: number; eqHighPassSlope: number; eqLowPassSlope: number;
  eqInputDb: number; eqOutputDb: number; eqPosition: number;
  slots: StudioSlot[]; sends: number[][];
}

/** Option index stored as (index + .01) / count, like the legacy selector. */
export const optionIndex = (value: number, count: number): number =>
  Math.min(count - 1, Math.floor(Math.max(0, Math.min(1, value)) * count));
export const optionValue = (index: number, count: number): number => (index + 0.01) / count;

export const reverbModel = (slot: StudioSlot): number => optionIndex(slot.params[5] ?? 0, 4);

// Room / Plate / Hall v2: diffusion, decay and stereo width (NordSlotFxRack.h).
const REVERB_V2_EXTRAS: readonly ControlDef[] = [[6, 'DIFFUSION', '%'], [7, 'DECAY', '%'], [8, 'WIDTH', '%']];
// Shimmer v3+: dual pitch-shifter voice (setMultiPitch); v4 adds BLOOM.
const SHIMMER_V3_EXTRAS: readonly ControlDef[] = [
  [16, 'PITCH B', 'st'], [17, 'B LEVEL', '%'], [20, 'A LEVEL', '%'],
  [18, 'GRAIN', 'ms'], [19, 'TONE', 'Hz'], [21, 'HARM WIDTH', 'width'],
];
const SHIMMER_V4_EXTRAS: readonly ControlDef[] = [[22, 'BLOOM', '%']];

/** Shimmer Atmosphere v4 (NordAtmosphericShimmer) needs slot version 4. */
export const SHIMMER_ATMOSPHERE_VERSION = 4;
export const SHIMMER_FREEZE_PARAM = 23;
export const isShimmer = (slot: StudioSlot): boolean => slot.type === 6 && reverbModel(slot) === 3;

export function slotControls(slot: StudioSlot): readonly ControlDef[] {
  if (slot.type === 6) {
    const model = reverbModel(slot);
    const base = REVERB_CONTROLS[model];
    if (model !== 3) return slot.version >= 2 ? [...base, ...REVERB_V2_EXTRAS] : base;
    return [
      ...base,
      ...(slot.version >= 3 ? SHIMMER_V3_EXTRAS : []),
      ...(slot.version >= SHIMMER_ATMOSPHERE_VERSION ? SHIMMER_V4_EXTRAS : []),
    ];
  }
  return (slot.version >= 2 && CONTROLS_ENHANCED[slot.type]) || CONTROLS[slot.type] || [];
}

export interface SlotOption { param: number; label: string; options: readonly string[] }
/** Discrete selectors shown next to the knobs for some algorithms. */
export function slotOptions(slot: StudioSlot): SlotOption[] {
  const out: SlotOption[] = [];
  if (slot.type === 6) {
    out.push({ param: 5, label: 'ALGORITHM', options: REVERB_TYPES });
    if (reverbModel(slot) === 3) {
      out.push({ param: 12, label: 'PITCH', options: SHIMMER_PITCH_MODES });
      out.push({ param: 13, label: 'STEREO', options: SHIMMER_SPATIAL_MODES });
      out.push({ param: 14, label: 'COLOR', options: SHIMMER_COLOR_MODES });
    }
  }
  if (slot.type === 5 && slot.version >= 3) out.push({ param: 7, label: 'DIVISION', options: NOTE_DIVISIONS });
  return out;
}

export function slotValueText(value: number, unit: string, type: number, param: number, version = 2): string {
  if (type === 12) {
    if (param === 0) return `${Math.round(value * 100)}%`;
    if (param === 1) return `${Math.round(20 * 50 ** value)} Hz`;
    if (param === 2) return `${Math.round(400 * 50 ** value)} Hz`;
    if (param >= 3 && param <= 5) return `${((value - 0.5) * 30).toFixed(1)} dB`;
    if (param >= 6 && param <= 8) {
      const lo = [50, 250, 1000][param - 6], hi = [500, 4000, 16000][param - 6];
      return `${Math.round(lo * (hi / lo) ** value)} Hz`;
    }
    if ([9, 13, 14].includes(param)) return `${(0.4 * 20 ** value).toFixed(2)} Q`;
    if (param === 12) return `${((value - 0.5) * 24).toFixed(1)} dB`;
  }
  if (type === 9) {
    if (param === 0) return `${(-42 + 40 * value).toFixed(1)} dB`;
    if (param === 1) return `${(1 + 19 * value).toFixed(1)}:1`;
    if (param === 2) return `${(18 * value).toFixed(1)} dB`;
    if (param === 3) return `${(1.5 + 95 * value).toFixed(1)} ms`;
    if (param === 4) return `${Math.round(35 + 750 * value)} ms`;
    if (param === 5) return `${(18 * value).toFixed(1)} dB`;
    if (param === 6) return value < 0.001 ? 'OFF' : `${Math.round(20 + 380 * value)} Hz`;
  }
  if (type === 2) {
    if (param === 4) return `${2 + 2 * Math.round(value * 3)} st`;
    if (param === 5) return `${Math.round(50 + 550 * value)} Hz`;
    if (param === 6) return `${Math.round(1000 + 7000 * value)} Hz`;
    if (param === 7) return `${Math.round(360 * value)}°`;
  }
  if (type === 1) {
    if (param === 6) return `${1 + Math.round(value * 2)} v`;
    if (param === 8) return `${Math.round(7 + 21 * value)} ms`;
  }
  if (type === 7) {
    if (param === 2) return `${Math.round(700 * 25 ** value)} Hz`;
    if (param === 4) return `${((value - 0.5) * 24).toFixed(1)} dB`;
  }
  if (type === 6) {
    // Units decide: p6–p8 are pitch/decay/low-cut on Shimmer but diffusion/
    // decay/width on Room, Plate and Hall. Formulas match NordSlotFxRack.h.
    if (unit === 'st' && param === 6) return `${((version >= 3 ? -24 : -12) + (version >= 3 ? 48 : 24) * value).toFixed(1)} st`;
    if (unit === 'st' && param === 16) return `${(-24 + 48 * value).toFixed(1)} st`;
    if (unit === 'ms' && param === 18) return `${Math.round(65 + 140 * value)} ms`;
    if (unit === 'ms' && param === 3) return `${Math.round(value * 240)} ms`;
    if (unit === 'Hz') {
      const hz = param === 8 ? 35 + 600 * value : param === 9 ? 1800 + 11500 * value
        : param === 10 ? 0.05 + 2.95 * value : param === 19 ? 300 * 55 ** value : NaN;
      if (!Number.isNaN(hz)) return hz >= 1000 ? `${(hz / 1000).toFixed(1)} kHz` : hz < 10 ? `${hz.toFixed(2)} Hz` : `${Math.round(hz)} Hz`;
    }
    if (unit === 'width') return `${Math.round(value * 200)}%`;
    if (param === 7 && unit === '%' && version >= 3) return `${Math.round(0.992 * value * 100)}%`;
    return `${Math.round(value * 100)}%`;
  }
  if (unit === 'Hz') {
    if (type === 1 && param === 5) return `${Math.round(2000 + 14000 * value)} Hz`;
    const base = type === 3 ? 0.5 : type === 5 ? 0 : 0.1;
    const span = type === 3 ? 13.5 : 5.9;
    return `${(base + value * span).toFixed(2)} Hz`;
  }
  if (unit === 'ms') return `${Math.round(type === 5 ? 40 + 1450 * value : type === 6 ? 240 * value : 2 + 80 * value)} ms`;
  if (unit === 'bits') return `${Math.round(4 + value * 12)} bits`;
  if (unit === 'x') return `${Math.round(1 + value * 31)}×`;
  if (unit === 'dB') return `${(param === 0 ? -36 + 34 * value : 12 * value).toFixed(1)} dB`;
  if (unit === ':1') return `${(1 + 15 * value).toFixed(1)}:1`;
  return `${Math.round(value * 100)}%`;
}
