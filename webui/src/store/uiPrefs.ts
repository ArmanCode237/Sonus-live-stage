// Per-browser interface preferences (not part of programs or presets).
import { useSyncExternalStore } from 'react';

export interface UiPrefs {
  /** Header monitor: MIDI input, voices, pedal and LIVE link. */
  statusBar: boolean;
  /** Zoom de la consola (sin modificar los tamaños/estilos originales). */
  uiZoom: number;
}
const KEY = 'liveStage.uiPrefs';
export const UI_ZOOM_OPTIONS = [65, 75, 85, 90, 100, 110, 125] as const;
const DEFAULTS: UiPrefs = { statusBar: true, uiZoom: 100 };
export function safeUiZoom(value: unknown): number {
  const n = typeof value === 'number' ? value : Number.NaN;
  return UI_ZOOM_OPTIONS.includes(n as typeof UI_ZOOM_OPTIONS[number]) ? n : 100;
}
const listeners = new Set<() => void>();

function read(): UiPrefs {
  try {
    const saved = JSON.parse(localStorage.getItem(KEY) ?? '{}') as Partial<UiPrefs>;
    return { ...DEFAULTS, ...saved, uiZoom: safeUiZoom(saved.uiZoom) };
  }
  catch { return { ...DEFAULTS }; }
}
let current: UiPrefs = read();

export function setUiPref<K extends keyof UiPrefs>(key: K, value: UiPrefs[K]): void {
  current = { ...current, [key]: key === 'uiZoom' ? safeUiZoom(value) : value };
  try { localStorage.setItem(KEY, JSON.stringify(current)); } catch { /* private mode: keep in memory */ }
  listeners.forEach(l => l());
}

export function useUiPrefs(): UiPrefs {
  return useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => current);
}
