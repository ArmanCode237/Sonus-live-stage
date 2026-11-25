// v32.3 end-to-end: drag a MOD source onto a knob, per-voice source rules, MOD / SEQ tabs, key-zone LEARN.
// Restores the modulation matrix, the zone and the active program afterwards.  node scripts/v323-smoke.mjs
import { chromium } from 'playwright-core';
const BASE = 'http://localhost:8080';
const SHOTS = process.env.SHOTS ? process.env.SHOTS + "/" : null;
const post = async (p, b) => (await fetch(BASE + p, { method: 'POST', body: JSON.stringify(b) })).json();
const get = async p => (await fetch(BASE + p)).json();
const results = [];
const check = (ok, label) => { results.push(`${ok ? 'PASS' : 'FAIL'}  ${label}`); };
const start = await get('/api/state');
const slot = start.activeProgramSlot;
const zoneBefore = start.params.synthLayers[1].keyLow;
const b = await chromium.launch({ executablePath: 'C:/Program Files/Google/Chrome/Application/chrome.exe' });
const p = await b.newPage({ viewport: { width: 1366, height: 768 } });
const errors = []; p.on('pageerror', e => errors.push(String(e))); p.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
try {
  await post('/api/synth/factory/load', { id: 1 });
  await post('/api/synth/mod', { connections: [] });
  await p.goto(BASE + '/synth', { waitUntil: 'domcontentloaded' }); await p.waitForSelector('.synth-workspace'); await p.waitForTimeout(900);
  check(await p.locator('.mod-chip').count() === 16, 'MOD rail shows 16 sources');
  if (SHOTS) await p.screenshot({ path: SHOTS + 'sound.png' });
  // drag WHEEL onto the CUTOFF knob
  const cutKnob = p.locator('.synth-editor .knob', { has: p.locator('.knob__label', { hasText: /^CUTOFF$/ }) }).first();
  await p.locator('.mod-chip', { hasText: 'WHEEL' }).dragTo(cutKnob);
  await p.waitForTimeout(700);
  const mod = await get('/api/synth/mod');
  check(mod.connections.some(c => c.source === 0 && c.target === 'L1_cut'), `drag WHEEL -> CUTOFF creates the connection (${JSON.stringify(mod.connections)})`);
  check(await cutKnob.locator('.knob__mod').count() === 1, 'the CUTOFF knob shows its modulation ring');
  // a per-voice source on a non-voice target is refused
  const resKnob = p.locator('.synth-editor .knob', { has: p.locator('.knob__label', { hasText: /^RESO$/ }) }).first();
  await p.locator('.mod-chip', { hasText: 'VELOCITY' }).dragTo(resKnob); await p.waitForTimeout(500);
  check(!(await get('/api/synth/mod')).connections.some(c => c.target === 'L1_res'), 'VELOCITY cannot be dropped on RESO (per-voice source)');
  await p.locator('.synth-deck__tabs .tabs__tab', { hasText: 'MATRIZ' }).click(); await p.waitForTimeout(400);
  check(await p.locator('.mod-row').count() >= 2, 'MATRIZ tab lists the connection');
  if (SHOTS) await p.screenshot({ path: SHOTS + 'mod.png' });
  await p.locator('.synth-deck__tabs .tabs__tab', { hasText: 'SEQ' }).click(); await p.waitForTimeout(400);
  check(await p.locator('.seq__global .btn, .seq__global button').count() >= 2, 'SEQ shows SYNC CLOCK and SHARE PATTERN');
  if (SHOTS) await p.screenshot({ path: SHOTS + 'seq.png' });
  // v32.4 per-layer output GAIN: knob in MIXER · OUT, stored in the layer, badge on the layer tab
  check(await p.locator('.synth-editor .knob[data-param="L1_gain"]').count() === 1, 'layer A has its GAIN knob');
  await post('/api/param', { id: 'L1_gain', value: 6 }); await p.waitForTimeout(500);
  check(Math.round((await get('/api/state')).params.synthLayers[0].outGainDb) === 6, 'L1_gain writes the layer output gain');
  check((await p.locator('.layer-tab').first().locator('.layer-tab__gain').textContent()) === '+6 dB', 'the layer tab shows +6 dB');
  await post('/api/param', { id: 'L1_gain', value: 0 });
  // v32.4 layer EQ in INSERTS: parameters reach the layer and the unit is shown
  for (const [id, v] of [['L1_eqOn', 1], ['L1_eqHp', 180], ['L1_eqHpSlope', 48], ['L1_eqLpSlope', 24], ['L1_eqG1', 6], ['L1_eqF1', 900], ['L1_eqQ1', 2]]) await post('/api/param', { id, value: v });
  await p.waitForTimeout(300);
  const eqL = (await get('/api/state')).params.synthLayers[0];
  check(eqL.eqEnabled === true && Math.round(eqL.eqHighPass) === 180 && eqL.eqHighPassSlope === 48 && eqL.eqLowPassSlope === 24
    && Math.round(eqL.eqGain[1]) === 6 && Math.round(eqL.eqFreq[1]) === 900 && Math.abs(eqL.eqQ[1] - 2) < .01, 'layer EQ parameters reach layer A');
  await post('/api/param', { id: 'L1_eqHpSlope', value: 36 }); await p.waitForTimeout(200);
  check((await get('/api/state')).params.synthLayers[0].eqHighPassSlope === 12, 'EQ slopes are 12 / 24 / 48 only');
  await p.locator('.synth-deck__tabs .tabs__tab', { hasText: 'INSERTS' }).click(); await p.waitForTimeout(300);
  check(await p.locator('.synth-eq .knob[data-param="L1_eqG2"]').count() === 1 && await p.locator('.synth-eq .segmented__item').count() === 6, 'INSERTS shows the EQ (3 bands, HP / LP slopes 12-24-48)');
  for (const [id, v] of [['L1_eqOn', 0], ['L1_eqHp', 20], ['L1_eqHpSlope', 12], ['L1_eqLpSlope', 12], ['L1_eqG1', 0], ['L1_eqF1', 1100], ['L1_eqQ1', 1]]) await post('/api/param', { id, value: v });
  // v32.4 synth module GAIN (patch bar) and the visual meters
  check(await p.locator('.patch-bar .knob[data-param="synthOutGainDb"]').count() === 1, 'patch bar has the synth GAIN knob');
  await post('/api/param', { id: 'synthOutGainDb', value: 3 }); await p.waitForTimeout(300);
  check(Math.round((await get('/api/state')).params.synthOutGainDb) === 3, 'synthOutGainDb writes the module output gain');
  await post('/api/param', { id: 'synthOutGainDb', value: 0 });
  const live = await get('/api/state');
  check(live.modulePeaks?.length === 3 && live.sourcePeaks?.length === 10, 'live state carries 3 module meters and 10 source meters');
  check(await p.locator('.module-tabs .level-meter').count() === 1 && await p.locator('.layer-tab .level-meter').count() === 4, 'module OUT meter and one meter per layer tab');
    // zones: LEARN LOW on synth layer B with a played key
  await p.goto(BASE + '/synth?tab=zones', { waitUntil: 'domcontentloaded' }); await p.waitForTimeout(900);
  const rowB = p.locator('.zone-row', { hasText: 'LAYER B' });
  await rowB.locator('.zone-learn').first().click(); await p.waitForTimeout(300);
  await post('/api/note', { key: 50, on: true, vel: 90 }); await post('/api/note', { key: 50, on: false });
  await p.waitForTimeout(900);
  const zone = (await get('/api/state')).params.synthLayers[1].keyLow;
  check(zone === 50, `LEARN LOW sets layer B low key from the played note (${zone})`);
  if (SHOTS) await p.screenshot({ path: SHOTS + 'zones.png' });
  check(errors.length === 0, `no JS errors ${errors.join(' | ')}`);
} finally {
  await post('/api/synth/mod', { connections: [] });
  await post('/api/param', { id: 'L2_keyLow', value: zoneBefore });
  await post('/api/programs/recall', { slot });
  await b.close();
}
console.log(results.join('\n'));
