// v33 MIDI routing helpers (pure; unit-tested). Destination bits match the engine:
// 0-4 piano layers L1-L5, 5-8 synth layers A-D, 9 organ.
import type { MidiRouteRule } from '../../api/client';

export const DEST_LABELS = ['L1', 'L2', 'L3', 'L4', 'L5', 'A', 'B', 'C', 'D', 'ORG'] as const;
export const PIANO_MASK = 0b0000011111;
export const SYNTH_MASK = 0b0111100000;
export const ORGAN_MASK = 0b1000000000;
export const ALL_MASK = 0b1111111111;

export const hasBit = (mask: number, bit: number) => (mask & (1 << bit)) !== 0;
export const toggleBit = (mask: number, bit: number) => (mask ^ (1 << bit)) & ALL_MASK;
/** A whole module: on if any of its bits is off, otherwise off. */
export const toggleGroup = (mask: number, group: number) => ((mask & group) === group ? mask & ~group : mask | group) & ALL_MASK;

/** "PIANO L1+L2 · SYNTH · ORGAN" style summary. */
export function describeDest(mask: number): string {
  if ((mask & ALL_MASK) === ALL_MASK) return 'TODO';
  if (!mask) return 'NADA';
  const part = (group: number, name: string, first: number, count: number) => {
    if ((mask & group) === group) return name;
    const on = Array.from({ length: count }, (_, i) => first + i).filter(b => hasBit(mask, b)).map(b => DEST_LABELS[b]);
    return on.length ? `${name} ${on.join('+')}` : '';
  };
  return [part(PIANO_MASK, 'PIANO', 0, 5), part(SYNTH_MASK, 'SYNTH', 5, 4), hasBit(mask, 9) ? 'ORGAN' : ''].filter(Boolean).join(' · ');
}

export const TEMPLATE_ALL: MidiRouteRule[] = [{ source: '*', channel: 0, dest: ALL_MASK }];
export const TEMPLATE_CHANNELS: MidiRouteRule[] = [
  { source: '*', channel: 1, dest: PIANO_MASK },
  { source: '*', channel: 2, dest: SYNTH_MASK },
  { source: '*', channel: 3, dest: ORGAN_MASK },
];
/** Keyboard 1 -> piano, keyboard 2 -> synth (+ the on-screen keyboard / DAW plays everything). */
export function templateKeyboards(ports: string[]): MidiRouteRule[] {
  const rules: MidiRouteRule[] = [{ source: 'internal', channel: 0, dest: ALL_MASK }];
  if (ports[0]) rules.push({ source: ports[0], channel: 0, dest: PIANO_MASK });
  if (ports[1]) rules.push({ source: ports[1], channel: 0, dest: SYNTH_MASK });
  if (ports[2]) rules.push({ source: ports[2], channel: 0, dest: ORGAN_MASK });
  return rules;
}

/** Destinations nobody reaches, and sources with no rule (silent) — shown as warnings. */
export function coverage(rules: MidiRouteRule[], sources: string[]): { silentSources: string[]; unreachedMask: number } {
  const any = rules.some(r => r.source === '*' && r.dest);
  const silentSources = any ? [] : sources.filter(s => !rules.some(r => r.source === s && r.dest));
  const reached = rules.reduce((m, r) => m | r.dest, 0);
  return { silentSources, unreachedMask: ALL_MASK & ~reached };
}
