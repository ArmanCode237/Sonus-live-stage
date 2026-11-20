// Piano Sounds / Performances end-to-end check (v31) against a running SonusLiveStage:
//  - a layer's inserts (reverb) and a SONIDO mapping are saved with its Sound;
//  - the Sound loaded into ANOTHER layer brings its effects and its mapping (retargeted);
//  - a Performance is a copy: its values win over later edits;
//  - module pages: EDITOR · KEYZONES · FX RACK; top bar grouped (instruments | tools).
// Uses a temporary Sound and Performance (deleted) and CC 105 on channel 16. At the end
// the active program is recalled again (read only), restoring the piano layers.
//   node scripts/piano-sound-smoke.mjs [baseUrl]
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
const live = () => get('/api/state');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const set = (id, value) => post('/api/param', { id, value });
const SOUND = 'Smoke Sound v31', PERF = 'Smoke Performance v31';

const start = await live();
const slot = start.activeProgramSlot;
const sample = (start.pianoLayers ?? []).find(l => l?.sourcePath);
if (!sample) { console.error('load a sample in any piano layer first'); process.exit(2); }
let soundId = null;

try {
  // 1. A fresh sample on L5 starts dry; edit it and give it a SONIDO mapping.
  await post('/api/load', { path: sample.sourcePath, layer: 4, sf2Preset: sample.sf2Preset ?? -1 });
  let s = await live();
  check(s.params.pianoSampleConfig[4].insertFx.every(u => !u.enabled), 'a newly loaded sample starts with its inserts off');
  for (const [id, v] of [['pianoFx4_5_on', 1], ['pianoFx4_5_type', 2], ['pianoFx4_5_mix', .4], ['pianoFx4_0_on', 1], ['pianoSample4Cut', 3000]]) await set(id, v);
  await post('/api/mappings', { op: 'assign', paramId: 'pianoSample4Cut', scope: 'sound', midi: { type: 'cc', channel: 16, number: 105 }, takeover: 'jump' });
  s = await live();
  check(s.soundDirty?.[4] === true, 'editing the layer marks its Sound as EDITADO');
  const saved = await post('/api/sounds/save', { module: 'piano', layer: 4, name: SOUND, variant: true });
  soundId = saved.sound?.id;
  check(Boolean(soundId), 'the Sound is saved');
  check((await live()).soundDirty?.[4] === false, 'saving the Sound clears its EDITADO');

  // 2. Load the Sound into another layer (L4): effects and mapping follow it.
  await post('/api/sounds/load', { id: soundId, layer: 3 });
  s = await live();
  const fx = s.params.pianoSampleConfig[3].insertFx;
  check(fx[5].enabled && fx[5].type === 2 && Math.abs(fx[5].mix - .4) < 1e-3 && fx[0].enabled, 'L4 gets the Sound inserts (hall reverb + chorus)');
  check(Math.abs(s.params.pianoSampleConfig[3].cutoffHz - 3000) < 1, 'L4 gets the Sound settings');
  const m = await get('/api/mappings');
  check(m.assignments.some(a => a.scope === 'sound' && a.paramId === 'pianoSample3Cut' && a.midi?.number === 105 && a.layer === 3),
    'the SONIDO mapping is retargeted to L4');
  await post('/api/mappings', { op: 'inject', midi: { type: 'cc', channel: 16, number: 105, value: 127 } });
  check(Math.abs((await live()).params.pianoSampleConfig[3].cutoffHz - 20000) < 1, 'the controller now drives L4 cutoff');

  // 3. Performance = copy of the module; its values win over later edits.
  await post('/api/piano/performances/store', { name: PERF });
  await set('pianoFx3_5_mix', .1);
  await post('/api/piano/performances/load', { name: PERF });
  s = await live();
  check(Math.abs(s.params.pianoSampleConfig[3].insertFx[5].mix - .4) < 1e-3, 'loading the Performance restores its copy of the Sound');
  check(s.moduleDirty?.[0] === false, 'the loaded Performance is not EDITADO');

  // 4. UI: module tabs, inserts tab, GUARDAR SONIDO, grouped top bar.
  const browser = await chromium.launch({ executablePath: BROWSERS.find(b => existsSync(b)), headless: true });
  const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  try {
    await page.goto(`${BASE}/piano`, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('.sample-editor'); await sleep(700);
    check(await page.locator('.nav__group--instruments .nav__link').count() === 4 && await page.locator('.nav__group--tools .nav__link').count() === 4,
      'top bar: 4 instruments | 4 tools, all visible');
    check(await page.locator('.sample-editor .btn', { hasText: 'GUARDAR SONIDO' }).count() === 1, 'the layer editor has GUARDAR SONIDO');
    await page.locator('.sample-editor .tabs__tab', { hasText: 'INSERTS' }).click();
    check(await page.locator('.sample-editor legend', { hasText: 'REVERB' }).count() === 1, 'the layer INSERTS tab includes the reverb unit');
    await page.locator('.module-tabs .subnav__item', { hasText: 'KEYZONES' }).click(); await sleep(300);
    check(page.url().includes('tab=zones') && await page.locator('.panel__title', { hasText: 'PIANO · KEYZONES' }).count() === 1, 'PIANO → KEYZONES shows only the piano zones');
    await page.locator('.module-tabs .subnav__item', { hasText: 'FX RACK' }).click(); await sleep(300);
    check(await page.locator('.rack').count() === 1, 'PIANO → FX RACK shows the piano rack');
    await page.goto(`${BASE}/synth?tab=fx`, { waitUntil: 'domcontentloaded' }); await sleep(800);
    check(await page.locator('.module-fx-tab .segmented__item', { hasText: 'SYNTH BUS FX' }).count() === 1, 'SYNTH → FX RACK offers the studio rack and the synth bus FX');
    await page.goto(`${BASE}/zones`, { waitUntil: 'domcontentloaded' }); await sleep(700);
    check(await page.locator('.view--zones a', { hasText: 'ABRIR EN ORGAN' }).count() === 1, 'ZONES links each module to its own page');
    check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  } finally { await browser.close(); }
} finally {
  if (soundId) await post('/api/sounds/delete', { id: soundId }).catch(() => {});
  await post('/api/piano/performances/delete', { name: PERF }).catch(() => {});
  for (const id of ['pianoSample3Cut', 'pianoSample4Cut']) await post('/api/mappings', { op: 'remove', paramId: id, scope: 'sound' }).catch(() => {});
  await post('/api/programs/recall', { slot });
}
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
