// Contract test: every parameter the engine registers must resolve to a numeric
// value in the serializeParams() snapshot (captured from a running engine).
import { describe, expect, it } from 'vitest';
import params from './__fixtures__/params.json';
import catalog from './__fixtures__/catalog.json';
import { readParam } from './paramMap';
import type { EngineParams } from '../api/types';

// Momentary triggers: they start an action and are not part of the edit buffer.
const TRIGGERS = /^programSlot\d+$/;

describe('readParam', () => {
  const p = params as unknown as EngineParams;
  it('resolves every registered parameter', () => {
    const missing = (catalog as { id: string }[])
      .map(e => e.id)
      .filter(id => !TRIGGERS.test(id) && readParam(p, id) === undefined);
    expect(missing).toEqual([]);
  });
  it('maps renamed and nested fields to the right values', () => {
    expect(readParam(p, 'tune')).toBe(p.masterTuneCents);
    expect(readParam(p, 'L1_cut')).toBe(p.synthLayers[0].cutoffHz);
    expect(readParam(p, 'L2_lfoRate')).toBe(p.synthLfos[1].rateHz);
    expect(readParam(p, 'pianoSample0Atk')).toBe(p.pianoSampleConfig[0].attackSec);
    expect(readParam(p, 'pianoSample1EqG2')).toBe(p.pianoSampleConfig[1].soundEqGain[2]);
    expect(readParam(p, 'organFxDelayTime')).toBe((p.organFx as Record<string, number>).delayMs);
    expect(readParam(p, 'pianoEnabled')).toBe(p.pianoEnabled ? 1 : 0);
    expect(readParam(p, 'drawbar4')).toBe(p.drawbars[4]);
  });
  it('returns undefined for unknown ids', () => {
    expect(readParam(p, 'doesNotExist')).toBeUndefined();
  });
});
