// v31 synth end-to-end check against a running SonusLiveStage (http://localhost:8080):
//  1. every vintage preset (97-144) loads through /api/synth/factory/load and the engine
//     state matches its JSON file;
//  2. the new parameter IDs (FM4, LA, transient, analog, LFO delay/sync) round-trip;
//  3. the SYNTH editor shows the FM4 operators and a knob writes the engine.
// The synth preset active before the test is restored at the end.
//   node scripts/synth-v31-smoke.mjs [baseUrl]
import { chromium } from 'playwright-core';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const BASE = process.argv[2] ?? 'http://localhost:8080';
const FACTORY = join(dirname(fileURLToPath(import.meta.url)), '../../presets/synth/factory');
const BROWSERS = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
];
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };
const post = async (path, body) => (await fetch(BASE + path, { method: 'POST', body: JSON.stringify(body) })).json();
const state = async () => (await (await fetch(`${BASE}/api/state`)).json()).params;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const near = (a, b) => Math.abs(Number(a) - Number(b)) < 1e-3;

const before = await state();
const originalPreset = Math.round(before.synthPresetId);

try {
  // 1. Vintage bank through the real HTTP path.
  const FIELDS = ['synthesisType', 'waveType', 'filterType', 'fm4Algorithm', 'transientType', 'analog', 'laMix', 'cutoffHz'];
  let loaded = 0;
  for (let id = 97; id <= 144; id++) {
    const file = JSON.parse(readFileSync(join(FACTORY, `${String(id).padStart(3, "0")}.json`), 'utf8'));
    const r = await post('/api/synth/factory/load', { id });
    const layer = r?.params?.synthLayers?.[0] ?? {};
    const want = file.params.layers[0];
    const bad = FIELDS.filter(f => !near(layer[f], want[f]));
    const ok = r.ok && r.params.synthPresetId === id && bad.length === 0
      && near(r.params.synthLayers[0].opRatio?.[3], want.opRatio[3]);
    if (!ok) check(false, `preset ${id} ${file.name}: ${r.error ?? bad.join(', ')}`); else loaded++;
  }
  check(loaded === 48, `48 vintage presets load and match their JSON (${loaded}/48)`);
  const status = await (await fetch(`${BASE}/api/synth/factory/status`)).json();
  check(status.maxFactoryPresets === 184, `factory status reports 184 presets (${status.maxFactoryPresets})`);

  // 2. New parameter IDs.
  const writes = [
    ['L2_synthType', 3, p => p.synthLayers[1].synthesisType], ['L2_fm4Algo', 5, p => p.synthLayers[1].fm4Algorithm],
    ['L2_op3Ratio', 3.5, p => p.synthLayers[1].opRatio[2]], ['L2_op4Dec', 2.25, p => p.synthLayers[1].opDecay[3]],
    ['L2_trType', 6, p => p.synthLayers[1].transientType], ['L2_trBright', .3, p => p.synthLayers[1].transientTone],
    ['L2_analog', .4, p => p.synthLayers[1].analog], ['L2_laRing', .2, p => p.synthLayers[1].laRing],
    ['L2_filtType', 4, p => p.synthLayers[1].filterType], ['L2_wave', 33, p => p.synthLayers[1].waveType],
    ['L2_lfoWave', 4, p => p.synthLfos[1].waveType], ['L2_lfoDelay', 1.5, p => p.synthLfos[1].delaySec],
    ['L2_lfoSync', 1, p => p.synthLfos[1].keySync],
  ];
  for (const [id, value] of writes) await post('/api/param', { id, value });
  await sleep(150);
  const p = await state();
  for (const [id, value, read] of writes) check(near(read(p), value), `${id} = ${value} (engine ${read(p)})`);

  // 3. Editor.
  const executablePath = BROWSERS.find(b => existsSync(b));
  if (!executablePath) throw new Error('No Chrome/Edge found');
  const browser = await chromium.launch({ executablePath, headless: true });
  const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  try {
    await post('/api/synth/factory/load', { id: 105 });   // DX7 · E.Piano Tines (FM4)
    await page.goto(`${BASE}/synth`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('.synth-workspace');
    await sleep(600);
    check(await page.locator('.patch-lcd__top').textContent().then(t => t.includes('105 / 184')), 'browser shows 105 / 184');
    const legends = await page.locator('.synth-editor legend').allTextContents();
    check(['OP 1', 'OP 2', 'OP 3', 'OP 4', 'FM4 · ALGORITHM'].every(l => legends.some(x => x.includes(l))), `FM4 editor groups (${legends.join(' | ')})`);
    const op2Ratio = page.locator('fieldset', { has: page.locator('legend', { hasText: 'OP 2' }) }).locator('.knob', { hasText: 'RATIO' }).locator('.knob__svg');
    const r0 = (await state()).synthLayers[0].opRatio[1];
    await op2Ratio.focus();
    for (let i = 0; i < 6; i++) await page.keyboard.press('ArrowUp');
    await sleep(400);
    const r1 = (await state()).synthLayers[0].opRatio[1];
    check(r1 > r0, `OP 2 RATIO knob writes the engine (${r0} → ${r1})`);
    await page.locator('.synth-deck__tabs .tabs__tab', { hasText: 'EXPRESIÓN' }).click();
    const wheel = await page.locator('.synth-deck .segmented', { hasText: 'VIBRATO' }).first().textContent();
    check(wheel.includes('VIB+CUSTOM') && wheel.includes('OFF'), `mod-wheel modes use engine order (${wheel})`);
    await page.locator('.patch-bar .btn', { hasText: 'BROWSER' }).click();
    await page.waitForSelector('.sb .sb__row');
    await page.locator('.sb__facet--factory').click(); await sleep(200);
    const cats = await page.locator('.sb__filters .sb__facet span').allTextContents();
    check(['D-50', 'DX7', 'JUPITER-8', 'JUNO-60', 'PROPHET-5', 'MINIMOOG'].every(c => cats.includes(c)), 'vintage categories in the factory browser');
    await page.locator('.sb__filters .sb__facet', { hasText: /^MINIMOOG\d+$/ }).click(); await sleep(200);
    check(await page.locator('.sb__row').count() === 8, 'MINIMOOG category lists 8 presets');
  } finally {
    await browser.close();
  }
  check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
} finally {
  await post('/api/synth/factory/load', { id: originalPreset });
}
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
