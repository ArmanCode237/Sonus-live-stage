// Visual theme: "stage" (red chassis, default) or "studio" (Studio Dark).
// Stored per browser; applied to <html data-theme> before the first render.
import { useSyncExternalStore } from 'react';

export type Theme = 'stage' | 'studio';
export const THEMES: readonly { id: Theme; label: string }[] = [
  { id: 'stage', label: 'STAGE' }, { id: 'studio', label: 'STUDIO DARK' },
];
const KEY = 'liveStage.theme';
const listeners = new Set<() => void>();

function read(): Theme {
  try { return localStorage.getItem(KEY) === 'studio' ? 'studio' : 'stage'; } catch { return 'stage'; }
}
let current: Theme = read();

export function applyTheme(theme: Theme = current): void {
  current = theme;
  // Always explicit: theme.css keys both palettes on the attribute.
  document.documentElement.setAttribute('data-theme', theme);
  try { localStorage.setItem(KEY, theme); } catch { /* private mode: keep in memory */ }
  listeners.forEach(l => l());
}

export function useTheme(): [Theme, (t: Theme) => void] {
  const theme = useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => current);
  return [theme, applyTheme];
}
