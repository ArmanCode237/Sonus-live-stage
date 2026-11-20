// Synth library end-to-end check against a running SonusLiveStage:
//  - favourites: ★ in SYNTH appears in ★ FAVORITOS on SYNTH and the PERFORM browser window; it offers ALL;
//  - GUARDAR stores an edit over a factory slot (name + sound) and RESTAURAR ORIGINAL reverts it;
//  - VOICE tab edits the voice settings that are in effect (global or per layer).
// Every favourite, factory edit and the active preset are restored at the end.
//   node scripts/synth-library-smoke.mjs [baseUrl]
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
const state = async () => (await get('/api/state')).params;
const sleep = ms => new Promise(r => setTimeout(r, ms));

const EDIT_SLOT = 112;      // must not already have a saved edit
const FAV_SLOT = 139;
const metaBefore = await get('/api/synth/factory/edits');
const originalPreset = Math.round((await state()).synthPresetId);
if (String(EDIT_SLOT) in metaBefore.overrides) { console.error(`slot ${EDIT_SLOT} already has a saved edit; choose another`); process.exit(2); }

const browser = await chromium.launch({ executablePath: BROWSERS.find(b => existsSync(b)), headless: true });
const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
const open = async route => {
  await page.goto(BASE + route, { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('.console__head');
  await sleep(700);
};

try {
  // ---------------------------------------------------------- favourites
  await open('/synth');
  await page.locator('.patch-bar .btn', { hasText: 'BROWSER' }).click();   // v32.4: library overlay
  const browserPanel = page.locator('.sb');
  await browserPanel.locator('.sb__row').first().waitFor();
  await browserPanel.locator('.sb__facet--factory').click();
  await browserPanel.locator('input[type=search]').fill('Fat Saw Lead');
  const star = browserPanel.locator('.sb__row', { hasText: 'Fat Saw Lead' }).first().locator('.star');
  const wasFav = metaBefore.favorites.includes(FAV_SLOT);
  if (wasFav) { await star.click(); await sleep(300); }
  await star.click(); await sleep(400);
  check((await get('/api/synth/factory/edits')).favorites.includes(FAV_SLOT), 'star in SYNTH stores the favourite');
  await browserPanel.locator('input[type=search]').fill('');
  await browserPanel.locator('.sb__facet', { hasText: '★ FAVORITOS' }).click(); await sleep(200);
  const favNames = await browserPanel.locator('.sb__name').allTextContents();
  check(favNames.some(n => n.includes('Fat Saw Lead')), `SYNTH ★ FAVORITOS lists it (${favNames.length} favourites)`);
  await page.keyboard.press('Escape'); await sleep(200);

  await open('/');
  const mini = page.locator('.synth-mini');
  const optionTexts = await mini.locator('select').locator('option').allTextContents();
  check(optionTexts[0] === 'ALL' && optionTexts[1] === '★ FAVORITOS', `PERFORM categories start with ALL, ★ FAVORITOS (${optionTexts.slice(0, 3).join(', ')})`);
  check(await mini.locator('.sb__row, .browser__item').count() === 0, 'PERFORM hides the preset list (it lives in the BROWSER window)');
  // The BROWSER button opens the preset window (modal, portalled to <body>).
  await mini.locator('.btn', { hasText: 'BROWSER' }).click();
  const modal = page.locator('.modal');
  await modal.waitFor();
  await modal.locator('.sb__row').first().waitFor();
  const favFacet = modal.locator('.sb__facet', { hasText: '★ FAVORITOS' });
  await favFacet.click(); await sleep(200);
  check((await modal.locator('.sb__name').allTextContents()).some(n => n.includes('Fat Saw Lead')), 'PERFORM BROWSER ★ FAVORITOS lists it');
  await favFacet.click();
  await modal.locator('.sb__facet--factory').click(); await sleep(200);
  check(await modal.locator('.sb__row').count() === 184, 'PERFORM BROWSER FÁBRICA lists the whole bank (184)');
  // un-star from PERFORM: both views share the same state
  await modal.locator('.sb__row', { hasText: 'Fat Saw Lead' }).first().locator('.star').click(); await sleep(400);
  check(!(await get('/api/synth/factory/edits')).favorites.includes(FAV_SLOT), 'star in PERFORM removes the favourite');
  await page.keyboard.press('Escape'); await sleep(200);
  check(await page.locator('.modal').count() === 0, 'Escape closes the BROWSER window');
  if (wasFav) await post('/api/synth/factory/meta', { op: 'favorite', id: FAV_SLOT, favorite: true });

  // ---------------------------------------------------------- GUARDAR / RESTAURAR
  const loaded = await post('/api/synth/factory/load', { id: EDIT_SLOT });
  const originalCut = loaded.params.synthLayers[0].cutoffHz;
  await post('/api/param', { id: 'L1_cut', value: 1234 });
  await open('/synth');
  // v32.4: GUARDAR offers a NEW user preset (default) or the factory slot.
  await page.locator('.patch-bar .btn', { hasText: 'GUARDAR' }).click();
  const dialog = page.locator('.modal__window');
  await dialog.locator('input').fill('Smoke New Preset');
  await dialog.locator('button[type=submit]').click();
  await sleep(500);
  check((await get('/api/synth/presets')).some(p => p.name === 'Smoke New Preset'), 'GUARDAR → NUEVO PRESET creates a user preset');
  check((await page.locator('.patch-lcd__name').textContent()) === 'Smoke New Preset', 'LCD shows the new user preset');
  check((await get('/api/synth/factory/edits')).overrides[EDIT_SLOT] === undefined, 'the factory slot is left untouched');
  await post('/api/synth/presets/delete', { name: 'Smoke New Preset' });
  await page.locator('.patch-bar .btn', { hasText: 'GUARDAR' }).click();
  await dialog.locator('.segmented__item', { hasText: 'SOBRE FÁBRICA' }).click();
  await dialog.locator('input').fill('Smoke Test Edit');
  await dialog.locator('button', { hasText: 'GUARDAR' }).click();
  await sleep(500);
  const afterSave = await get('/api/synth/factory/edits');
  check(afterSave.overrides[EDIT_SLOT]?.name === 'Smoke Test Edit', 'GUARDAR stores the edit over the factory slot');
  check((await page.locator('.patch-lcd__name').textContent()) === 'Smoke Test Edit', 'LCD shows the saved name');
  check(await page.locator('.patch-lcd__top').textContent().then(t => t.includes('USUARIO')), 'LCD marks the slot as a saved user edit');
  await post('/api/synth/factory/load', { id: 1 });
  const reloaded = await post('/api/synth/factory/load', { id: EDIT_SLOT });
  check(Math.round(reloaded.params.synthLayers[0].cutoffHz) === 1234, `reloading the slot recalls the saved sound (${reloaded.params.synthLayers[0].cutoffHz} Hz)`);
  await page.locator('.patch-bar .btn', { hasText: 'GUARDAR' }).click();
  await page.locator('.modal__window .segmented__item', { hasText: 'SOBRE FÁBRICA' }).click();
  await page.locator('.modal__window button', { hasText: 'RESTAURAR ORIGINAL' }).click();
  await sleep(700);
  check(!(String(EDIT_SLOT) in (await get('/api/synth/factory/edits')).overrides), 'RESTAURAR ORIGINAL removes the saved edit');
  check(Math.abs((await state()).synthLayers[0].cutoffHz - originalCut) < 1, 'and reloads the original sound');

  // ---------------------------------------------------------- VOICE tab
  await post('/api/synth/factory/load', { id: 137 });   // Minimoog · Classic Bass: global LEGATO
  await open('/synth');
  await page.locator('.synth-deck__tabs .tabs__tab', { hasText: 'VOICE' }).click();
  const group = page.locator('fieldset', { has: page.locator('legend', { hasText: 'MODE · GLIDE' }) });
  check(await group.locator('legend').textContent().then(t => t.includes('GLOBAL')), 'VOICE shows the global scope for a global preset');
  check(await group.locator('.segmented__item.is-active', { hasText: 'LEGATO' }).count() === 1, 'VOICE shows LEGATO (the mode in effect)');
  await group.locator('.segmented__item', { hasText: 'MONO' }).click(); await sleep(300);
  check(Math.round((await state()).synthVoiceMode) === 1, 'changing the mode writes the global voice mode');
  await group.locator('.segmented__item', { hasText: 'POR CAPA' }).click(); await sleep(300);
  check((await state()).synthVoiceMaster === false && await group.locator('legend').textContent().then(t => t.includes('CAPA A')), 'POR CAPA switches to the layer settings');
  await group.locator('.segmented__item', { hasText: 'MONO' }).click(); await sleep(300);
  check(Math.round((await state()).synthLayers[0].voiceMode) === 1, 'changing the mode now writes layer A');
  check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
} finally {
  await browser.close();
  const meta = await get('/api/synth/factory/edits');
  if (String(EDIT_SLOT) in meta.overrides) await post('/api/synth/factory/meta', { op: 'reset', id: EDIT_SLOT });
  const fav = meta.favorites.includes(FAV_SLOT);
  if (fav !== metaBefore.favorites.includes(FAV_SLOT)) await post('/api/synth/factory/meta', { op: 'favorite', id: FAV_SLOT, favorite: !fav });
  await post('/api/synth/factory/load', { id: originalPreset });
}
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
