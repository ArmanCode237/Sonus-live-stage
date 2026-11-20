// v32.5 checks against a running SonusLiveStage:
//  - per-layer OCTAVE / SEMI for piano layers (pianoZone<n>Oct / Semi) and synth layers (L<n>_oct / _semi):
//    engine params, KEYZONES capsules, PERFORM layer badge;
//  - the synth / organ preset name lives in the engine (state.presetRefs): save, load, rename, delete,
//    factory load, and it is shown on PERFORM (program display + module LCDs) after a page reload.
// Everything it changes is restored at the end.
//   node scripts/v325-smoke.mjs [baseUrl]
import { chromium } from 'playwright-core';
import { existsSync } from 'node:fs';

const BASE = process.argv[2] ?? 'http://localhost:8080';
const BROWSERS = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
];
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };
const post = async (path, body) => (await fetch(BASE + path, { method: 'POST', body: JSON.stringify(body) })).json();
const get = async path => (await fetch(BASE + path)).json();
const state = () => get('/api/state');
const setParam = (id, value) => post('/api/param', { id, value });
const sleep = ms => new Promise(r => setTimeout(r, ms));

const NAME = 'zz v325 smoke', NAME2 = 'zz v325 smoke renamed';
const before = await state();
const originalPreset = Math.round(before.params.synthPresetId);
const originalPianoZone = before.params.pianoLayerZones[0];
const originalSynthOct = before.params.synthLayers[0].octave, originalSynthSemi = before.params.synthLayers[0].semitones;
if ((await get('/api/synth/presets')).some(p => p.name === NAME || p.name === NAME2)) { console.error('test preset already exists'); process.exit(2); }

const browser = await chromium.launch({ executablePath: BROWSERS.find(b => existsSync(b)), headless: true });
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
const open = async route => { await page.goto(BASE + route, { waitUntil: 'domcontentloaded' }); await page.waitForSelector('.console__head'); await sleep(800); };
const labelOf = sel => page.locator(sel).first().evaluate(e => `${e.textContent} ${[...e.querySelectorAll('[aria-label]')].map(x => x.getAttribute('aria-label')).join(' ')}`);
/** Waits (up to 4 s) until the element's text / pixel labels contain `text` (live state arrives over the socket). */
const shows = async (sel, text) => {
  for (let i = 0; i < 20; i++) { if ((await labelOf(sel)).includes(text)) return true; await sleep(200); }
  return false;
};

try {
  // ---------------------------------------------------------- engine: piano layer octave / semi
  check('presetRefs' in before, 'state carries presetRefs');
  await setParam('pianoZone0Oct', 1); await setParam('pianoZone0Semi', -3); await sleep(200);
  let z = (await state()).params.pianoLayerZones[0];
  check(z.octave === 1 && z.transpose === -3, `piano L1 OCT/SEMI stored (${z.octave}, ${z.transpose})`);
  await setParam('pianoZone0Oct', 9); await sleep(150);
  check((await state()).params.pianoLayerZones[0].octave === 2, 'piano layer octave is clamped to ±2');

  // ---------------------------------------------------------- engine: preset names
  await post('/api/synth/presets/save', { name: NAME });
  check((await state()).presetRefs?.synth?.name === NAME, 'saving a user preset names the playing synth');
  await post('/api/synth/factory/load', { id: originalPreset });
  check((await state()).presetRefs?.synth === null, 'a factory load clears the name');
  await post('/api/synth/presets/load', { name: NAME });
  check((await state()).presetRefs?.synth?.name === NAME, 'loading the user preset names it');
  await post('/api/synth/presets/rename', { name: NAME, to: NAME2 });
  check((await state()).presetRefs?.synth?.name === NAME2, 'renaming the playing preset renames it on the displays');

  // ---------------------------------------------------------- UI: PERFORM after a reload
  await open('/');
  check(await shows('.program-lcd', NAME2), 'PERFORM program display shows the user preset');
  check(await shows('.synth-mini .mini-lcd', NAME2), 'PERFORM synth LCD shows the user preset');
  check((await page.locator('.layer-row').first().textContent()).includes('+2 OCT -3 ST') ||
    !(await state()).pianoLayers?.[0]?.sourcePath, 'PERFORM L1 row shows the layer shift (when it holds a sample)');

  const organ = (await get('/api/organ/factory'))[0];
  if (organ) {
    await post('/api/organ/factory/load', { id: organ.factoryId });
    await sleep(600);
    check((await state()).presetRefs?.organ?.name === organ.name, `organ factory load names the organ (${organ.name})`);
    check(await shows('.organ-mini .mini-lcd', organ.name), 'PERFORM organ LCD shows it');
  }

  // ---------------------------------------------------------- UI: KEYZONES capsules
  await open('/zones');
  await setParam('pianoZone0Oct', 0); await setParam('pianoZone0Semi', 0); await sleep(300);
  await page.getByRole('button', { name: 'L1 octava +1' }).click();
  await page.getByRole('button', { name: 'L1 semitono +1' }).click(); await sleep(300);
  z = (await state()).params.pianoLayerZones[0];
  check(z.octave === 1 && z.transpose === 1, 'KEYZONES L1 OCT / SEMI buttons write the engine');
  await page.getByRole('button', { name: 'LAYER A octava −1' }).click(); await sleep(300);
  check((await state()).params.synthLayers[0].octave === originalSynthOct - 1 || originalSynthOct === -2, 'KEYZONES LAYER A OCT writes L1_oct');
  check(await page.locator('.pitch-stepper.is-shifted').count() >= 1, 'a shifted layer is highlighted');
  check(await page.locator('.pitch-stepper').count() === 9, 'one OCT / SEMI capsule per layer (5 piano + 4 synth)');

  await post('/api/synth/presets/delete', { name: NAME2 });
  check((await state()).presetRefs?.synth === null, 'deleting the playing preset clears the name');
  check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
} finally {
  await browser.close();
  for (const n of [NAME, NAME2]) await post('/api/synth/presets/delete', { name: n });
  await post('/api/synth/factory/load', { id: originalPreset });
  await setParam('L1_oct', originalSynthOct); await setParam('L1_semi', originalSynthSemi);
  await setParam('pianoZone0Oct', originalPianoZone.octave ?? 0); await setParam('pianoZone0Semi', originalPianoZone.transpose ?? 0);
}
console.log(failures.length ? `\n${failures.length} FAILED` : '\nALL PASS');
process.exit(failures.length ? 1 : 0);
