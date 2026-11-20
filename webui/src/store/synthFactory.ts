// Synth factory metadata shared by SYNTH and PERFORM: favourites and saved edits
// (user overrides of factory slots). The engine file is the source of truth; every
// mutation replaces the local copy with the metadata the engine returns.
import { useSyncExternalStore } from 'react';
import { api } from '../api/client';
import type { FactoryMeta } from '../api/types';
import { SYNTH_FACTORY, type FactoryPatch } from '../data/synthFactory';
import { runAction } from './notify';

interface State { favorites: ReadonlySet<number>; overrides: FactoryMeta['overrides']; loaded: boolean }
let state: State = { favorites: new Set(), overrides: {}, loaded: false };
const listeners = new Set<() => void>();
let pending: Promise<void> | null = null;

function apply(meta: FactoryMeta | undefined) {
  if (!meta) return;
  state = { favorites: new Set(meta.favorites ?? []), overrides: meta.overrides ?? {}, loaded: true };
  listeners.forEach(l => l());
}

export function loadFactoryMeta(force = false): Promise<void> {
  if (!force && (state.loaded || pending)) return pending ?? Promise.resolve();
  pending = api.synthFactoryMeta().then(apply).catch(() => {}).finally(() => { pending = null; });
  return pending;
}

export function useFactoryMeta(): State {
  const snapshot = useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => state);
  if (!snapshot.loaded) void loadFactoryMeta();
  return snapshot;
}

/** Name shown for a factory slot: the saved edit's name when the slot was edited. */
export function displayName(patch: FactoryPatch | undefined, overrides: FactoryMeta['overrides']): string {
  if (!patch) return '—';
  return overrides[String(patch.id)]?.name || patch.name;
}
export const isEdited = (id: number, overrides: FactoryMeta['overrides']) => String(id) in overrides;

export async function toggleFavorite(id: number): Promise<void> {
  const favorite = !state.favorites.has(id);
  // Optimistic: the star reacts immediately; the engine response is authoritative.
  const next = new Set(state.favorites);
  if (favorite) next.add(id); else next.delete(id);
  state = { ...state, favorites: next };
  listeners.forEach(l => l());
  const r = await runAction(() => api.synthFactoryFavorite(id, favorite), favorite ? 'Añadido a favoritos' : 'Quitado de favoritos');
  if (r) apply(r.metadata); else void loadFactoryMeta(true);
}

export async function saveFactoryEdit(id: number, name: string): Promise<boolean> {
  const r = await runAction(() => api.synthFactorySave(id, name), `Preset ${String(id).padStart(3, '0')} guardado`);
  if (r) apply(r.metadata);
  return Boolean(r);
}

export async function restoreFactoryOriginal(id: number): Promise<boolean> {
  const r = await runAction(() => api.synthFactoryReset(id), 'Original restaurado');
  if (!r) return false;
  apply(r.metadata);
  // Reload the slot so the edit buffer matches the bundled preset again.
  await runAction(() => api.loadSynthFactory(id), undefined, true);
  return true;
}

export const FAVORITES_CATEGORY = '★ FAVORITOS';
export const ALL_CATEGORY = 'ALL';

/** Patches of a browser category: ALL, ★ FAVORITOS or a factory category; `query` searches all names. */
export function filterFactory(category: string, query: string, favorites: ReadonlySet<number>, overrides: FactoryMeta['overrides']): FactoryPatch[] {
  const q = query.trim().toLowerCase();
  return SYNTH_FACTORY.filter(p => {
    if (category === FAVORITES_CATEGORY && !favorites.has(p.id)) return false;
    if (category !== ALL_CATEGORY && category !== FAVORITES_CATEGORY && !q && p.category !== category) return false;
    return !q || displayName(p, overrides).toLowerCase().includes(q) || p.name.toLowerCase().includes(q);
  });
}
