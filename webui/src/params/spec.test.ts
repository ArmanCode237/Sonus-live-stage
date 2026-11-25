import { describe, expect, it } from 'vitest';
import { formatValue, fromNorm, inferUnit, makeSpec, toNorm } from './spec';

describe('parameter specs', () => {
  it('infers units from engine ids', () => {
    expect(inferUnit('L1_cut')).toBe('Hz');
    expect(inferUnit('pianoSample0Atk')).toBe('s');
    expect(inferUnit('pianoPan')).toBe('pan');
    expect(inferUnit('pianoLayerEnabled0')).toBe('bool');
    expect(inferUnit('eqGain2')).toBe('dB');
    expect(inferUnit('L2_semi')).toBe('st');
    expect(inferUnit('organModel')).toBe('int');
  });
  it('uses a logarithmic response for wide frequency ranges', () => {
    const cut = makeSpec('L1_cut', 40, 20000);
    expect(cut.log).toBe(true);
    expect(toNorm(cut, 40)).toBe(0);
    expect(toNorm(cut, 20000)).toBeCloseTo(1);
    expect(fromNorm(cut, toNorm(cut, 1000))).toBeCloseTo(1000, 3);
  });
  it('snaps integer parameters and clamps to the range', () => {
    const model = makeSpec('organModel', 0, 7);
    expect(fromNorm(model, 0.51)).toBe(4);
    expect(fromNorm(model, 2)).toBe(7);
  });
  it('centres bipolar parameters on zero', () => {
    expect(makeSpec('pianoPan', -1, 1).defaultValue).toBe(0);
    expect(makeSpec('pianoSample0Tune', -100, 100).defaultValue).toBe(0);
  });
  it('formats readouts', () => {
    expect(formatValue(makeSpec('L1_cut', 40, 20000), 1500)).toBe('1.5 kHz');
    expect(formatValue(makeSpec('pianoSample0Atk', 0.005, 5), 0.25)).toBe('250 ms');
    expect(formatValue(makeSpec('pianoPan', -1, 1), -0.5)).toBe('L50');
    expect(formatValue(makeSpec('pianoVolume', 0, 1.5), 1.2)).toBe('120 %');
    expect(formatValue(makeSpec('eqGain0', -15, 15), 3)).toBe('+3 dB');
  });
});
