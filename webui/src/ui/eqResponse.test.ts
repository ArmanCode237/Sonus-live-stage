import { describe, expect, it } from 'vitest';
import { eqMagnitudeDb, slotEqModel, type EqModel } from './eqResponse';

const flatBands = [{ freqHz: 100, q: 1, gainDb: 0 }, { freqHz: 1000, q: 1, gainDb: 0 }, { freqHz: 8000, q: 1, gainDb: 0 }];
const at = (m: EqModel, hz: number) => eqMagnitudeDb(m, [hz])[0];

describe('eqResponse (mirrors MasterEqualizer)', () => {
  it('is flat with zero gains and the filters parked', () => {
    const m: EqModel = { enabled: true, hpHz: 20, lpHz: 20000, bands: flatBands };
    for (const hz of [30, 200, 1000, 5000, 15000]) expect(Math.abs(at(m, hz))).toBeLessThan(1e-6);
  });
  it('peaks a band by its gain at its centre', () => {
    const m: EqModel = { enabled: true, bands: [{ freqHz: 1000, q: 1.4, gainDb: 12 }] };
    expect(at(m, 1000)).toBeCloseTo(12, 1);
    expect(at(m, 60)).toBeLessThan(1);
  });
  it('Butterworth corners are -3 dB and slopes steepen 12 → 24 → 48', () => {
    for (const slope of [12, 24, 48]) expect(at({ enabled: true, hpHz: 200, hpSlope: slope, bands: [] }, 200)).toBeCloseTo(-3.01, 1);
    const octaveBelow = (slope: number) => at({ enabled: true, hpHz: 400, hpSlope: slope, bands: [] }, 100);
    expect(octaveBelow(24)).toBeLessThan(octaveBelow(12) - 10);
    expect(octaveBelow(48)).toBeLessThan(octaveBelow(24) - 20);
  });
  it('treats 36 dB/oct as 12, like the engine', () => {
    const m = (slope: number) => at({ enabled: true, lpHz: 2000, lpSlope: slope, bands: [] }, 6000);
    expect(m(36)).toBeCloseTo(m(12), 6);
  });
  it('bypass draws flat', () => {
    expect(at({ enabled: false, hpHz: 500, bands: [{ freqHz: 1000, q: 1, gainDb: 15 }], gainDb: 6 }, 1000)).toBeCloseTo(0, 6);
  });
  it('parametric slot: neutral at defaults, wet mixes the delta', () => {
    const neutral = [1, 0, 1, .5, .5, .5, .5, .5, .5, .5, 0, 0, .5, .5, .5];
    expect(Math.abs(at(slotEqModel(neutral), 1000))).toBeLessThan(.05);
    const boost = [...neutral]; boost[4] = 1; boost[7] = Math.log(1000 / 250) / Math.log(4000 / 250); // band 2 +15 dB at 1 kHz
    expect(at(slotEqModel(boost), 1000)).toBeCloseTo(15, 0);
    expect(at(slotEqModel([0, ...boost.slice(1)]), 1000)).toBeCloseTo(0, 6);
  });
});
