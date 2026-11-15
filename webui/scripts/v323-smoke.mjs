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
