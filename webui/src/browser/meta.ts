// Sound-browser metadata shared by the three modules (engine file browser_meta.json):
// tags, category overrides, favourites, recents and usage counts per sound key.
import { useEffect, useSyncExternalStore } from 'react';
import { api } from '../api/client';
import { notify } from '../store/notify';

export interface BrowserItemMeta { tags?: string[]; category?: string; favorite?: boolean }
export interface BrowserMeta {
  items: Record<string, BrowserItemMeta>;
  usage: Record<string, { count: number; last: number }>;
}

let meta: BrowserMeta = { items: {}, usage: {} };
let loaded = false;
let loading: Promise<void> | null = null;
const listeners = new Set<() => void>();
const emit = () => listeners.forEach(l => l());

export function loadBrowserMeta(force = false): Promise<void> {
  if (loading && !force) return loading;
  loading = api.browserMeta()
    .then(m => { meta = { items: m.items ?? {}, usage: m.usage ?? {} }; loaded = true; emit(); })
    .catch(() => { loading = null; });
  return loading;
}

export function useBrowserMeta(): BrowserMeta {
  useEffect(() => { if (!loaded) void loadBrowserMeta(); }, []);
  return useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => meta);
}

async function send(body: Record<string, unknown>, quiet = false): Promise<boolean> {
  try {
    const r = await api.setBrowserMeta(body);
    if (r.meta) { meta = { items: r.meta.items ?? {}, usage: r.meta.usage ?? {} }; emit(); }
    return true;
  } catch (e) {
    if (!quiet) notify((e as Error).message, 'error');
    return false;
  }
}

/** Optimistic local update, then the engine (the UI never waits for the file). */
function patchLocal(key: string, patch: Partial<BrowserItemMeta>) {
  const next = { ...(meta.items[key] ?? {}), ...patch };
  meta = { ...meta, items: { ...meta.items, [key]: next } };
  emit();
}

export const setTags = (key: string, tags: string[]) => { patchLocal(key, { tags }); return send({ op: 'tags', key, tags }); };
export const setCategory = (key: string, category: string) => { patchLocal(key, { category: category || undefined }); return send({ op: 'category', key, category }); };
export const setMetaFavorite = (key: string, favorite: boolean) => { patchLocal(key, { favorite }); return send({ op: 'favorite', key, favorite }); };
export const renameKey = (from: string, to: string) => send({ op: 'rename', from, to }, true);
export const forgetKey = (key: string) => send({ op: 'forget', key }, true);

/** A sound was loaded: recents and "most used". */
export function recordUse(key: string): void {
  const u = meta.usage[key] ?? { count: 0, last: 0 };
  meta = { ...meta, usage: { ...meta.usage, [key]: { count: u.count + 1, last: Date.now() } } };
  emit();
  void send({ op: 'use', key }, true);
}
