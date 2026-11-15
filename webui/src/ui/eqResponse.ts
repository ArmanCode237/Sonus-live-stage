// Frequency response of the engine equalizers (src/dsp/NordDspEffects.h MasterEqualizer):
// Butterworth HP/LP cascades (12/24/48 dB/oct; any other slope behaves as 12, like
// normalizeEqSlope) + RBJ peaking bands, evaluated as complex biquads at 48 kHz.

export interface EqBandModel { freqHz: number; q: number; gainDb: number }
export interface EqModel {
  enabled: boolean;
  /** High pass corner; only active above 22 Hz (as in the engine). */
  hpHz?: number; hpSlope?: number;
  /** Low pass corner; only active below 19.5 kHz. */
  lpHz?: number; lpSlope?: number;
  bands: EqBandModel[];
  /** Gain applied after the filters (input + output trims), in dB. */
  gainDb?: number;
  /** Parametric slot "delta return": H_eff = 1 + wet · (H·g − 1). Omit for a plain EQ. */
  wet?: number;
}

const SR = 48000;
type C = [number, number];
type Biquad = [b0: number, b1: number, b2: number, a1: number, a2: number];

const BUTTERWORTH_Q: Record<number, number[]> = {
  1: [0.70710678],
  2: [0.54119610, 1.30656296],
  4: [0.50979558, 0.60134489, 0.89997622, 2.56291545],
};
export const normalizeEqSlope = (slope: number | undefined) => (slope === 48 ? 48 : slope === 24 ? 24 : 12);

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));
const norm = (b0: number, b1: number, b2: number, a0: number, a1: number, a2: number): Biquad => [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];

function highPass(f: number, q: number): Biquad {
  const w0 = 2 * Math.PI * clamp(f, 20, SR * 0.45) / SR, c = Math.cos(w0), al = Math.sin(w0) / (2 * q);
  return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
function lowPass(f: number, q: number): Biquad {
  const w0 = 2 * Math.PI * clamp(f, 40, SR * 0.48) / SR, c = Math.cos(w0), al = Math.sin(w0) / (2 * q);
  return norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
function peaking(f: number, q: number, gainDb: number): Biquad {
  const A = Math.pow(10, gainDb / 40);
  const w0 = 2 * Math.PI * clamp(f, 20, SR * 0.45) / SR, c = Math.cos(w0), al = Math.sin(w0) / (2 * Math.max(q, 0.05));
  return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

/** The biquads the engine runs for this model (empty when bypassed). */
export function eqSections(m: EqModel): Biquad[] {
  if (!m.enabled) return [];
  const out: Biquad[] = [];
  if ((m.hpHz ?? 0) > 22) for (const q of BUTTERWORTH_Q[normalizeEqSlope(m.hpSlope) / 12]) out.push(highPass(m.hpHz!, q));
  if ((m.lpHz ?? 20000) < 19500) for (const q of BUTTERWORTH_Q[normalizeEqSlope(m.lpSlope) / 12]) out.push(lowPass(m.lpHz!, q));
  for (const b of m.bands) if (Math.abs(b.gainDb) > 1e-4) out.push(peaking(b.freqHz, b.q, b.gainDb));
  return out;
}

const mul = (a: C, b: C): C => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
const div = (a: C, b: C): C => { const d = b[0] * b[0] + b[1] * b[1] || 1e-30; return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d]; };

/** Complex response H(e^jw) of the cascade at `hz`. */
function response(sections: Biquad[], hz: number): C {
  const w = 2 * Math.PI * hz / SR;
  const z1: C = [Math.cos(w), -Math.sin(w)], z2: C = [Math.cos(2 * w), -Math.sin(2 * w)];
  let h: C = [1, 0];
  for (const [b0, b1, b2, a1, a2] of sections) {
    const num: C = [b0 + b1 * z1[0] + b2 * z2[0], b1 * z1[1] + b2 * z2[1]];
    const den: C = [1 + a1 * z1[0] + a2 * z2[0], a1 * z1[1] + a2 * z2[1]];
    h = mul(h, div(num, den));
  }
  return h;
}

/** Magnitude in dB of the whole equalizer (filters, bands, trims, wet mix) at each frequency. */
export function eqMagnitudeDb(m: EqModel, freqs: readonly number[]): number[] {
  const sections = eqSections(m);
  const g = m.enabled ? Math.pow(10, (m.gainDb ?? 0) / 20) : 1;
  return freqs.map(f => {
    let h = response(sections, f);
    h = [h[0] * g, h[1] * g];
    if (m.wet !== undefined) h = [1 + m.wet * (h[0] - 1), m.wet * h[1]];
    return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-6));
  });
}

/** `n` log-spaced frequencies between `lo` and `hi`. */
export function logFrequencies(n: number, lo = 20, hi = 20000): number[] {
  return Array.from({ length: n }, (_, i) => lo * Math.pow(hi / lo, i / (n - 1)));
}

/** PARAMETRIC EQ studio slot (type 12): normalized params → model (NordSlotFxRack syncParameters). */
export function slotEqModel(p: readonly number[]): EqModel {
  const c = (i: number) => clamp(p[i] ?? 0, 0, 1);
  const lo = [50, 250, 1000], hi = [500, 4000, 16000], qParam = [9, 13, 14];
  const slope = (v: number) => 12 * (1 << clamp(Math.floor(v * 3), 0, 2));
  return {
    enabled: true,
    hpHz: 20 * Math.pow(50, c(1)), hpSlope: slope(p[10] ?? 0),
    lpHz: 400 * Math.pow(50, c(2)), lpSlope: slope(p[11] ?? 0),
    bands: [0, 1, 2].map(i => ({ freqHz: lo[i] * Math.pow(hi[i] / lo[i], c(6 + i)), q: 0.4 * Math.pow(20, c(qParam[i])), gainDb: (c(3 + i) - 0.5) * 30 })),
    gainDb: ((p[12] ?? 0.5) - 0.5) * 24,
    wet: c(0),
  };
}
