// Studio rack writes (POST /api/studio-racks). Same policy as parameters:
// optimistic local value, trailing throttle per control, engine state wins
// once it reflects the change. Rack state itself is read from params.studioRacks.
import { useSyncExternalStore } from 'react';
import { api } from '../api/client';
import type { Json } from '../api/types';
import { engine } from './engine';
import { notify } from './notify';

const THROTTLE_MS = 50;
const HOLD_MS = 900;

const overrides = new Map<string, { value: number; at: number }>();
const timers = new Map<string, ReturnType<typeof setTimeout>>();
const pending = new Map<string, Json>();
const inFlight = new Map<string, Promise<unknown>>();
const commandsInFlight = new Set<Promise<unknown>>();
let writesSuspended = false;
let version = 0;
const listeners = new Set<() => void>();
const emit = () => { version++; listeners.forEach(l => l()); };
const subscribe = (l: () => void) => { listeners.add(l); return () => listeners.delete(l); };

async function flush(key: string) {
  timers.delete(key);
  if (inFlight.has(key)) return;
  const body = pending.get(key);
  if (!body) return;
  pending.delete(key);
  const request = api.studio(body);
  inFlight.set(key, request);
  try { await request; }
  catch (e) { notify((e as Error).message, 'error'); overrides.delete(key); emit(); }
  finally {
    inFlight.delete(key);
    if (pending.has(key) && !writesSuspended && !timers.has(key))
      timers.set(key, setTimeout(() => void flush(key), THROTTLE_MS));
    const o = overrides.get(key);
    if (o) o.at = Date.now();
    engine.refresh();
  }
}

/** Continuous control (knob/fader): throttled, optimistic. */
export function studioSet(key: string, body: Json, value: number): void {
  if (writesSuspended) return;
  overrides.set(key, { value, at: Date.now() + 60_000 });
  pending.set(key, body);
  emit();
  if (!timers.has(key) && !inFlight.has(key)) timers.set(key, setTimeout(() => void flush(key), THROTTLE_MS));
}

/** Discrete action (type change, swap, power): sent immediately. */
export async function studioCommand(body: Json): Promise<boolean> {
  if (writesSuspended) return false;
  const request = api.studio(body);
  commandsInFlight.add(request);
  try { await request; engine.refresh(); return true; }
  catch (e) { notify((e as Error).message, 'error'); return false; }
  finally { commandsInFlight.delete(request); }
}

/** Optimistic value for a rack control, falling back to the engine value. */
export function useStudioValue(key: string, engineValue: number | undefined): number | undefined {
  useSyncExternalStore(subscribe, () => version);
  useSyncExternalStore(engine.subscribe, () => engine.version);
  const o = overrides.get(key);
  if (!o) return engineValue;
  const settled = engineValue !== undefined && Math.abs(engineValue - o.value) < 1e-4;
  if (!pending.has(key) && (settled || Date.now() - o.at > HOLD_MS)) { overrides.delete(key); return engineValue; }
  return o.value;
}

/** Prevent stale rack writes from reaching the next Sound/Performance. */
export async function beginStudioEditTransaction(replace: boolean): Promise<() => void> {
  writesSuspended = true;
  for (const timer of timers.values()) clearTimeout(timer);
  timers.clear();
  if (replace) { pending.clear(); overrides.clear(); emit(); }
  while (pending.size || inFlight.size || commandsInFlight.size) {
    if (!replace) for (const key of Array.from(pending.keys())) {
      if (!inFlight.has(key)) void flush(key);
    }
    const active = [...inFlight.values(), ...commandsInFlight.values()];
    if (active.length) await Promise.allSettled(active);
    else await Promise.resolve();
  }
  return () => { writesSuspended = false; };
}
