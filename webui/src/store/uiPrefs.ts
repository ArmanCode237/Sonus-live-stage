// Per-browser interface preferences (not part of programs or presets).
import { useSyncExternalStore } from 'react';

export interface UiPrefs {
  /** Header monitor: MIDI input, voices, pedal and LIVE link. */
  statusBar: boolean;
}
const KEY = 'liveStage.uiPrefs';
const DEFAULTS: UiPrefs = { statusBar: true };
const listeners = new Set<() => void>();

function read(): UiPrefs {
  try { return { ...DEFAULTS, ...(JSON.parse(localStorage.getItem(KEY) ?? '{}') as Partial<UiPrefs>) }; }
  catch { return { ...DEFAULTS }; }
}
let current: UiPrefs = read();

export function setUiPref<K extends keyof UiPrefs>(key: K, value: UiPrefs[K]): void {
  current = { ...current, [key]: value };
  try { localStorage.setItem(KEY, JSON.stringify(current)); } catch { /* private mode: keep in memory */ }
  listeners.forEach(l => l());
}

export function useUiPrefs(): UiPrefs {
  return useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => current);
}
