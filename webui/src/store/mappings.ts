// MIDI / OSC mappings (v31) as seen by the UI: every scope's assignments, the
// effective one per parameter (PROGRAM > MODULE > SYSTEM) and conflicts with SYSTEM.
// The engine is the source of truth; live state carries a revision that triggers a refetch.
import { useSyncExternalStore } from 'react';
import { api } from '../api/client';
import type { Assignment, MappingConflict, MappingState, MapScope, MidiSource } from '../api/types';
import { notify } from './notify';

export const SCOPE_LABEL: Record<MapScope, string> = { system: 'SISTEMA', sound: 'SONIDO', module: 'PRESET', program: 'PROGRAMA' };
export const MODULE_LABEL = ['PIANO', 'SYNTH', 'ORGAN'];

interface Snapshot {
  state: MappingState | null;
  byParam: ReadonlyMap<string, Assignment[]>;
  effective: ReadonlyMap<string, Assignment>;
}
let snapshot: Snapshot = { state: null, byParam: new Map(), effective: new Map() };
const listeners = new Set<() => void>();
let loadedRevision = -1;
let inflight: Promise<void> | null = null;
let conflictSignature = '';

function apply(state: MappingState) {
  const byParam = new Map<string, Assignment[]>();
  const effective = new Map<string, Assignment>();
  for (const a of state.assignments) {
    byParam.set(a.paramId, [...(byParam.get(a.paramId) ?? []), a]);
    if (a.effective) effective.set(a.paramId, a);
  }
  snapshot = { state, byParam, effective };
  loadedRevision = state.revision;
  announceConflicts(state.conflicts);
  listeners.forEach(l => l());
}

/** Alerts once whenever a new set of SYSTEM conflicts appears (preset / program loads, new mappings). */
function announceConflicts(conflicts: MappingConflict[]) {
  const signature = conflicts.map(c => `${c.source}:${c.others.map(o => o.paramId).join(',')}`).sort().join('|');
  if (signature && signature !== conflictSignature)
    notify(`⚠ Mensajes también mapeados en SISTEMA: ${conflicts.map(describeConflict).join(' · ')}`, 'error', 9000);
  conflictSignature = signature;
}

export function describeConflict(c: MappingConflict): string {
  const sys = c.system.map(r => r.paramId).join(', ');
  const other = c.others.map(r => `${r.paramId} (${refLabel(r)})`).join(', ');
  return `${c.source} → SISTEMA: ${sys} / ${other}`;
}
/** Piano module presets are Performances; synth / organ presets are presets. */
export const modulePresetLabel = (module: number) => (module === 0 ? 'PERFORMANCE' : `PRESET ${MODULE_LABEL[module] ?? ''}`);
export const refLabel = (r: { scope: MapScope; module?: number; layer?: number }) =>
  r.scope === 'module' ? modulePresetLabel(r.module ?? 0)
    : r.scope === 'sound' ? `SONIDO L${(r.layer ?? 0) + 1}` : SCOPE_LABEL[r.scope];

// Parameters owned by a piano layer Sound (same families as ParamValueReader::soundLayerOfParam).
const SOUND_PARAMS = [/^pianoSample([0-4])\w+$/, /^pianoFx([0-4])_\d_\w+$/, /^fxSend([0-4])(Mod|Delay|Reverb)$/, /^studioSend0_([0-4])_\d+$/];
/** Piano layer whose Sound owns the parameter, or -1. */
export function soundLayerOf(id: string): number {
  for (const re of SOUND_PARAMS) { const m = id.match(re); if (m) return Number(m[1]); }
  return -1;
}

export function loadMappings(): Promise<void> {
  if (inflight) return inflight;
  inflight = api.mappings().then(apply).catch(() => {}).finally(() => { inflight = null; });
  return inflight;
}
/** Called with the live-state revision; refetches when it changed. */
export function syncMappingRevision(revision: number | undefined) {
  if (revision !== undefined && revision !== loadedRevision) void loadMappings();
}
export function setMappingState(state: MappingState) { apply(state); }

export function useMappings(): Snapshot {
  return useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => snapshot);
}

export function sourceLabel(a: Pick<Assignment, 'midi' | 'osc'>): string {
  const parts: string[] = [];
  if (a.midi) parts.push(midiLabel(a.midi));
  if (a.osc) parts.push(a.osc);
  return parts.join(' + ');
}
export function midiLabel(m: MidiSource): string {
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const what = m.type === 'note' ? `NOTE ${names[m.number % 12]}${Math.floor(m.number / 12) - 1}` : `CC ${m.number}`;
  return `${what}${m.channel ? ` · ch ${m.channel}` : ''}`;
}
export const midiOverlaps = (a: MidiSource | null, b: MidiSource | null) =>
  Boolean(a && b && a.type === b.type && a.number === b.number && (a.channel === 0 || b.channel === 0 || a.channel === b.channel));

/** Effective assignments (other than `paramId` in `scope`) that would share this source. */
export function sharedWith(source: { midi: MidiSource | null; osc: string | null }, paramId: string): Assignment[] {
  const out: Assignment[] = [];
  for (const a of snapshot.effective.values()) {
    if (a.paramId === paramId) continue;
    if (midiOverlaps(a.midi, source.midi) || (source.osc && a.osc === source.osc)) out.push(a);
  }
  return out;
}

/** Attributes for a mappable control's root element (context menu target + badge). */
export type MapAttrs = { 'data-param': string; 'data-map-scope'?: MapScope; 'data-map'?: string };
export function mapAttrsFor(effective: ReadonlyMap<string, Assignment>, id: string): MapAttrs {
  const a = effective.get(id);
  return a ? { 'data-param': id, 'data-map-scope': a.scope, 'data-map': sourceLabel(a) } : { 'data-param': id };
}
export function useMapAttrs(id: string): MapAttrs {
  return mapAttrsFor(useMappings().effective, id);
}
