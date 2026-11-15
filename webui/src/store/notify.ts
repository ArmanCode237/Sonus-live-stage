// Transient notifications for action results (store, load, save…).
import { useSyncExternalStore } from 'react';
import { engine } from './engine';
import { beginStudioEditTransaction } from './studio';
import type { EngineParams } from '../api/types';

export interface Toast { id: number; text: string; kind: 'ok' | 'error' | 'info' }

let toasts: Toast[] = [];
let seq = 0;
const listeners = new Set<() => void>();
const emit = () => listeners.forEach(l => l());

export function notify(text: string, kind: Toast['kind'] = 'info', ms = kind === 'error' ? 5000 : 2200): void {
  const toast = { id: ++seq, text, kind };
  toasts = [...toasts.slice(-3), toast];
  emit();
  setTimeout(() => { toasts = toasts.filter(t => t.id !== toast.id); emit(); }, ms);
}

export function useToasts(): Toast[] {
  return useSyncExternalStore(cb => { listeners.add(cb); return () => listeners.delete(cb); }, () => toasts);
}

/**
 * Run an engine action: report success/failure and apply any params snapshot
 * the backend returns, so the UI reflects the new state immediately.
 */
// Sequential state changes: an older, slower sample load must never finish
// after a newer program recall and unexpectedly overwrite the edit buffer.
let actionQueue: Promise<void> = Promise.resolve();

export async function runAction<T>(
  action: () => Promise<T>, success?: string, replaceEditBuffer = false
): Promise<T | undefined> {
  const previous = actionQueue;
  let releaseQueue!: () => void;
  actionQueue = new Promise<void>(resolve => { releaseQueue = resolve; });
  await previous;
  let releaseEngine: (() => void) | undefined;
  let releaseStudio: (() => void) | undefined;
  try {
    releaseEngine = await engine.beginEditTransaction(replaceEditBuffer);
    releaseStudio = await beginStudioEditTransaction(replaceEditBuffer);
    const result = await action();
    const params = (result as { params?: EngineParams } | undefined)?.params;
    if (params) engine.applyParams(params); else engine.refresh();
    if (success) notify(success, 'ok');
    return result;
  } catch (e) {
    notify((e as Error).message || 'Error', 'error');
    return undefined;
  } finally {
    releaseStudio?.();
    releaseEngine?.();
    releaseQueue();
  }
}
