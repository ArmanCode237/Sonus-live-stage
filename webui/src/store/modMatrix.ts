// Synth modulation matrix (v32.3): connections live in the engine (params.modMatrix);
// the UI drags a source chip onto any modulatable knob of the synth layers.
// Mirrors src/modules/SynthModMatrix.h.
import { useSyncExternalStore } from 'react';
import { api } from '../api/client';
import { useEngine } from './hooks';
import { notify, runAction } from './notify';

export interface ModConnection { source: number; target: string; amount: number; bipolar: boolean; curve: number }

export const MOD_SOURCES = [
  { id: 0, name: 'WHEEL', voice: false, bipolar: false, color: '#ffb02e' },
  { id: 1, name: 'BEND', voice: false, bipolar: true, color: '#ff7a2e' },
  { id: 2, name: 'AFTERTOUCH', voice: false, bipolar: false, color: '#ff4fa8' },
  { id: 3, name: 'VELOCITY', voice: true, bipolar: false, color: '#9cff3a' },
  { id: 4, name: 'KEY', voice: true, bipolar: true, color: '#4fe3a1' },
  { id: 5, name: 'AMP ENV', voice: true, bipolar: false, color: '#2ee6c8' },
  { id: 6, name: 'FILTER ENV', voice: true, bipolar: false, color: '#3fd0ff' },
  { id: 7, name: 'LFO A', voice: false, bipolar: true, color: '#b07cff' },
  { id: 8, name: 'LFO B', voice: false, bipolar: true, color: '#b07cff' },
  { id: 9, name: 'LFO C', voice: false, bipolar: true, color: '#b07cff' },
  { id: 10, name: 'LFO D', voice: false, bipolar: true, color: '#b07cff' },
  { id: 11, name: 'RANDOM', voice: false, bipolar: true, color: '#f2d33b' },
  { id: 12, name: 'MACRO 1', voice: false, bipolar: false, color: '#ff6a6a' },
  { id: 13, name: 'MACRO 2', voice: false, bipolar: false, color: '#ff6a6a' },
  { id: 14, name: 'MACRO 3', voice: false, bipolar: false, color: '#ff6a6a' },
  { id: 15, name: 'MACRO 4', voice: false, bipolar: false, color: '#ff6a6a' },
] as const;
export const MOD_CURVES = ['LIN', 'EXP', 'LOG', 'S'];
export const MOD_MAX = 32;

/** Modulatable layer parameters: suffix after "L<n>_" -> label. Voice targets accept every source. */
export const MOD_TARGETS: Record<string, string> = {
  cut: 'CUTOFF', vol: 'LEVEL', pan: 'PAN', semi: 'PITCH', det: 'FINE', res: 'RESO', filtEnv: 'FILT ENV', filtKT: 'FILT KEY',
  hpf: 'HPF', drive: 'DRIVE', atk: 'ATTACK', dec: 'DECAY', sus: 'SUSTAIN', rel: 'RELEASE', fAtk: 'F.ATTACK', fDec: 'F.DECAY',
  fSus: 'F.SUSTAIN', fRel: 'F.RELEASE', saw: 'SAW', pulse: 'PULSE', pw: 'PW', pwmDepth: 'PWM', pwmRate: 'PWM RATE', sub: 'SUB',
  noise: 'NOISE', osc2: 'OSC2', osc2Det: 'OSC2 DET', fmIndex: 'FM INDEX', fmRatio: 'FM RATIO', wtMorph: 'WT MORPH',
  uniDet: 'UNI DETUNE', uniSprd: 'UNI SPREAD', laMix: 'LA MIX', laRing: 'RING', analog: 'ANALOG', portamento: 'GLIDE',
  wheelVibrato: 'VIBRATO', seqGate: 'SEQ GATE',
  ...Object.fromEntries([0, 1, 2, 3, 4, 5].flatMap(u => ['mix', 'rate', 'depth', 'feedback'].map(f => [`fx${u}_${f}`, `FX${u + 1} ${f.toUpperCase()}`]))),
};
const VOICE_TARGETS = new Set(['cut', 'vol', 'pan', 'semi']);

/** {layer, suffix, voice} when the parameter can be modulated. */
export function modTarget(id: string): { layer: number; suffix: string; voice: boolean } | null {
  const m = /^L([1-4])_(.+)$/.exec(id);
  if (!m || !(m[2] in MOD_TARGETS)) return null;
  return { layer: +m[1] - 1, suffix: m[2], voice: VOICE_TARGETS.has(m[2]) };
}
export const modTargetLabel = (id: string) => {
  const t = modTarget(id);
  return t ? `${'ABCD'[t.layer]} · ${MOD_TARGETS[t.suffix]}` : id;
};
export const modSource = (id: number) => MOD_SOURCES[id] ?? MOD_SOURCES[0];

export function useModConnections(): ModConnection[] {
  return useEngine(s => (s?.params as { modMatrix?: ModConnection[] } | undefined)?.modMatrix) ?? EMPTY;
}
const EMPTY: ModConnection[] = [];

export async function saveModConnections(list: ModConnection[], message?: string): Promise<boolean> {
  return Boolean(await runAction(() => api.synthModSet(list.slice(0, MOD_MAX)), message ?? 'Modulación actualizada'));
}

/** Adds source -> target (or selects it if it already exists). */
export async function connectMod(list: ModConnection[], source: number, target: string): Promise<void> {
  const t = modTarget(target);
  const src = modSource(source);
  if (!t) { notify('Este control no se puede modular', 'error'); return; }
  if (src.voice && !t.voice) { notify(`${src.name} es por voz: solo modula CUTOFF, LEVEL, PAN o PITCH`, 'error'); return; }
  if (list.some(c => c.source === source && c.target === target)) { notify('Esa conexión ya existe'); return; }
  if (list.length >= MOD_MAX) { notify(`Máximo ${MOD_MAX} conexiones`, 'error'); return; }
  await saveModConnections([...list, { source, target, amount: .25, bipolar: src.bipolar, curve: 0 }], `${src.name} → ${modTargetLabel(target)}`);
}

// ---------------------------------------------------------------- drag state (highlights drop targets)
let dragging: number | null = null;
const listeners = new Set<() => void>();
export function setModDragging(source: number | null) {
  dragging = source;
  document.body.classList.toggle('is-mod-dragging', source !== null);
  listeners.forEach(l => l());
}
export const useModDragging = () => useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => dragging);
export const MOD_DRAG_TYPE = 'application/x-live-stage-mod';
