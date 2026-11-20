import { expect, test } from 'vitest';
import { inferUnit } from './spec';
test('v32.4 synth EQ / gain units', () => {
  expect(['L1_eqOn', 'L1_eqHp', 'L1_eqLp', 'L1_eqHpSlope', 'L1_eqF0', 'L1_eqQ1', 'L1_eqG2', 'L1_gain', 'synthOutGainDb'].map(inferUnit))
    .toEqual(['bool', 'Hz', 'Hz', 'int', 'Hz', 'x', 'dB', 'dB', 'dB']);
});
