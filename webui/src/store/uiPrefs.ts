// Per-browser interface preferences (not part of programs or presets).
import { useSyncExternalStore } from 'react';

export interface UiPrefs {
  /** Header monitor: MIDI input, voices, pedal and LIVE link. */
  statusBar: boolean;
  /** AUTO: la consola se ajusta al tamaño de la ventana/pantalla (lienzo de referencia 1280×800). */
  uiAutoFit: boolean;
  /** Tamaño de la interfaz en % (relativo al ajuste AUTO, o absoluto con AUTO apagado). */
  uiScale: number;
}
const KEY = 'liveStage.uiPrefs';
export const UI_SCALE_OPTIONS = [75, 85, 90, 100, 110, 120, 135, 150] as const;
const DEFAULTS: UiPrefs = { statusBar: true, uiAutoFit: true, uiScale: 100 };

export function safeUiScale(value: unknown): number {
  const n = typeof value === 'number' ? value : Number.NaN;
  return UI_SCALE_OPTIONS.includes(n as typeof UI_SCALE_OPTIONS[number]) ? n : 100;
}
/** Paso siguiente/anterior de la escala (atajos ⌘/Ctrl + y −). */
export function stepUiScale(current: number, dir: 1 | -1): number {
  const list = UI_SCALE_OPTIONS as readonly number[];
  const i = list.indexOf(safeUiScale(current));
  return list[Math.max(0, Math.min(list.length - 1, i + dir))];
}

const listeners = new Set<() => void>();

function read(): UiPrefs {
  try {
    // `uiZoom` (versiones anteriores) era un zoom CSS absoluto con otro significado: se descarta.
    const saved = JSON.parse(localStorage.getItem(KEY) ?? '{}') as Partial<UiPrefs> & { uiZoom?: unknown };
    return {
      statusBar: typeof saved.statusBar === 'boolean' ? saved.statusBar : DEFAULTS.statusBar,
      uiAutoFit: typeof saved.uiAutoFit === 'boolean' ? saved.uiAutoFit : DEFAULTS.uiAutoFit,
      uiScale: safeUiScale(saved.uiScale),
    };
  }
  catch { return { ...DEFAULTS }; }
}
let current: UiPrefs = read();

export function getUiPrefs(): UiPrefs { return current; }

export function setUiPref<K extends keyof UiPrefs>(key: K, value: UiPrefs[K]): void {
  current = { ...current, [key]: key === 'uiScale' ? safeUiScale(value) : value };
  try { localStorage.setItem(KEY, JSON.stringify(current)); } catch { /* private mode: keep in memory */ }
  listeners.forEach(l => l());
}

export function subscribeUiPrefs(cb: () => void): () => void {
  listeners.add(cb);
  return () => { listeners.delete(cb); };
}

export function useUiPrefs(): UiPrefs {
  return useSyncExternalStore(subscribeUiPrefs, () => current);
}
