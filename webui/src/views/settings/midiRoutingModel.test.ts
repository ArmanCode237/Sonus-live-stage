import { describe, expect, it } from 'vitest';
import { ALL_MASK, coverage, describeDest, ORGAN_MASK, PIANO_MASK, SYNTH_MASK, templateKeyboards, toggleBit, toggleGroup } from './midiRoutingModel';

describe('midi routing model', () => {
  it('describes destinations', () => {
    expect(describeDest(ALL_MASK)).toBe('TODO');
    expect(describeDest(0)).toBe('NADA');
    expect(describeDest(PIANO_MASK | ORGAN_MASK)).toBe('PIANO · ORGAN');
    expect(describeDest(0b11 | (1 << 6))).toBe('PIANO L1+L2 · SYNTH B');
  });
  it('toggles bits and whole modules', () => {
    expect(toggleBit(0, 9)).toBe(ORGAN_MASK);
    expect(toggleGroup(1, PIANO_MASK)).toBe(PIANO_MASK);
    expect(toggleGroup(PIANO_MASK | SYNTH_MASK, PIANO_MASK)).toBe(SYNTH_MASK);
  });
  it('builds the keyboard template and finds silent inputs', () => {
    const rules = templateKeyboards(['MOXF', 'Studio']);
    expect(rules.map(r => [r.source, r.dest])).toEqual([['internal', ALL_MASK], ['MOXF', PIANO_MASK], ['Studio', SYNTH_MASK]]);
    expect(coverage(rules, ['internal', 'MOXF', 'Studio', 'Pads']).silentSources).toEqual(['Pads']);
    expect(coverage([{ source: '*', channel: 1, dest: PIANO_MASK }], ['x']).unreachedMask).toBe(SYNTH_MASK | ORGAN_MASK);
  });
});
