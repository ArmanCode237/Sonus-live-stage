// React bindings for the engine store. Selectors must return primitives or
// references that stay stable while unchanged (e.g. state.params).
import { useCallback, useSyncExternalStore } from 'react';
import type { EngineState } from '../api/types';
import { engine } from './engine';
import type { ParamSpec } from '../params/spec';

export function useEngine<T>(selector: (s: EngineState | null) => T): T {
  return useSyncExternalStore(engine.subscribe, () => selector(engine.state));
}

export function useConnection() {
  return useSyncExternalStore(engine.subscribe, () => engine.connection);
}

/** 'ws' while the real-time socket is up, 'http' on the polling fallback. */
export function useTransport() {
  return useSyncExternalStore(engine.subscribe, () => engine.transport);
}

export interface ParamBinding {
  value: number | undefined;
  spec: ParamSpec | undefined;
  set: (value: number) => void;
}

/** Bind a control to an engine parameter by ID. */
export function useParam(id: string): ParamBinding {
  const value = useSyncExternalStore(engine.subscribe, () => engine.value(id));
  const spec = useSyncExternalStore(engine.subscribe, () => engine.spec(id));
  const set = useCallback((v: number) => engine.set(id, v), [id]);
  return { value, spec, set };
}
