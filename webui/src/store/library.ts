// Shared sample-library index (GET /api/library), loaded once per session and
// reused by the Main sound bank, the Piano page and the full browser.
import { useEffect, useSyncExternalStore } from 'react';
import { api } from '../api/client';
import type { LibraryItem } from '../api/types';

let items: LibraryItem[] | null = null;
let error = '';
let loading: Promise<void> | null = null;
const listeners = new Set<() => void>();
const emit = () => listeners.forEach(l => l());
const subscribe = (l: () => void) => { listeners.add(l); return () => listeners.delete(l); };

export function loadLibrary(force = false): Promise<void> {
  if (loading && !force) return loading;
  loading = api.library()
    .then(r => { items = r.items; error = ''; })
    .catch(e => { error = (e as Error).message; loading = null; })
    .finally(emit);
  return loading;
}

export function patchLibraryItem(path: string, patch: Partial<LibraryItem>): void {
  items = items?.map(i => (i.path === path ? { ...i, ...patch } : i)) ?? null;
  emit();
}

export function useLibrary(): { items: LibraryItem[] | null; error: string } {
  useEffect(() => { void loadLibrary(); }, []);
  const snapshot = useSyncExternalStore(subscribe, () => items);
  const err = useSyncExternalStore(subscribe, () => error);
  return { items: snapshot, error: err };
}

/** Library groups in display order (alphabetical), without empty entries. */
export function libraryGroups(list: LibraryItem[] | null): string[] {
  return Array.from(new Set((list ?? []).map(i => i.group).filter(Boolean))).sort();
}
