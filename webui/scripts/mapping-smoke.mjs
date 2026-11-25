// MIDI / OSC mapping end-to-end check (v31) against a running SonusLiveStage.
// Virtual MIDI goes through POST /api/mappings {op:"inject"}, i.e. the same code path
// as the MIDI callback. Uses CC 102-106 on channel 16 and a temporary synth preset;
// everything created is removed at the end. Programs are never stored.
//   node scripts/mapping-smoke.mjs [baseUrl]
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
const post = async (path, body) => { const r = await fetch(BASE + path, { method: 'POST', body: JSON.stringify(body) }); return r.json(); };
const get = async path => (await fetch(BASE + path)).json();
const params = async () => (await get('/api/state')).params;
const live = async () => get('/api/state');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const cc = (number, value, channel = 16) => post('/api/mappings', { op: 'inject', midi: { type: 'cc', channel, number, value } });
const find = (state, paramId, scope) => state.assignments.find(a => a.paramId === paramId && a.scope === scope);
const TEMP_PRESET = 'Mapping Smoke Test';

const before = await get('/api/mappings');
const originalPreset = Math.round((await params()).synthPresetId);
const used = before.assignments.filter(a => a.midi && a.midi.number >= 102 && a.midi.number <= 106);
if (used.length) { console.error('CC 102-106 already mapped; aborting to keep your mappings intact'); process.exit(2); }
// A global parameter without a SYSTEM mapping: assigning it must never replace one of yours.
const SYS_PARAM = ['tune', 'transpose', 'masterBpm'].find(id => !before.assignments.some(a => a.paramId === id && a.scope === 'system'));
if (!SYS_PARAM) { console.error('no free global parameter for the SYSTEM conflict test'); process.exit(2); }

const browser = await chromium.launch({ executablePath: BROWSERS.find(b => existsSync(b)), headless: true });
const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
const open = async route => { await page.goto(BASE + route, { waitUntil: 'domcontentloaded' }); await page.waitForSelector('.console__head'); await sleep(700); };
const learnWith = async (selector, inject) => {
  await page.locator(selector).first().click({ button: 'right' });
  await page.waitForSelector('.mapping-menu');
  await page.locator('.mapping-menu button', { hasText: 'APRENDER' }).click();
  await page.waitForSelector('.mapping-menu__learn');
  await inject();
  await sleep(600);
};

try {
  await post('/api/synth/factory/load', { id: 1 });
  await open('/synth');

  // 1. Right-click → APRENDER → CC: a PRESET (synth module) mapping by default.
  await learnWith('.synth-editor .knob[data-param="L1_vol"]', () => cc(102, 20));
  let m = await get('/api/mappings');
  const vol = find(m, 'L1_vol', 'module');
  check(Boolean(vol && vol.midi?.number === 102 && vol.midi?.channel === 0 && vol.module === 1), 'learn assigns CC 102 (omni) to L1_vol in the SYNTH preset scope');
  check(vol?.takeover === 'pickup', 'new mappings default to pickup');
  await page.keyboard.press('Escape');
  await sleep(300);
  check(await page.locator('.knob[data-param="L1_vol"][data-map-scope="module"]').count() > 0, 'the knob shows the PRESET mapping badge');
  check((await live()).moduleDirty?.[1] === true, 'a new preset mapping marks the synth as EDITADO');

  // 2. Pickup: far from the current value nothing happens, crossing it takes over.
  await post('/api/param', { id: 'L1_vol', value: .75 });
  await sleep(150);
  await cc(102, 120); await cc(102, 125);
  check(Math.abs((await params()).synthLayers[0].volume - .75) < 1e-3, 'pickup: CC far above the value does not jump');
  await cc(102, 40);                                        // crosses .75 (= 63.5 of 1.5)
  const picked = (await params()).synthLayers[0].volume;
  check(Math.abs(picked - 1.5 * 40 / 127) < 1e-3, `pickup: crossing the value takes over (${picked.toFixed(3)})`);
  await cc(102, 90);
  check(Math.abs((await params()).synthLayers[0].volume - 1.5 * 90 / 127) < 1e-3, 'after pickup the parameter follows the control');

  // 3. Conflict with SYSTEM: confirmation before assigning, then a toast.
  await post('/api/mappings', { op: 'assign', paramId: SYS_PARAM, scope: 'system', midi: { type: 'cc', channel: 16, number: 103 } });
  await learnWith('.synth-editor .knob[data-param="L1_pan"]', () => cc(103, 64));
  const alert = page.locator('.mapping-menu__alert');
  check(await alert.count() === 1 && (await alert.textContent()).includes(SYS_PARAM), 'learning a SYSTEM message asks for confirmation and names the parameter');
  await page.locator('.mapping-menu button', { hasText: 'ASIGNAR IGUALMENTE' }).click();
  await sleep(700);
  m = await get('/api/mappings');
  check(m.conflicts.some(c => c.source.includes('CC 103') && c.system[0].paramId === SYS_PARAM && c.others[0].paramId === 'L1_pan'), 'the engine reports the conflict');
  check(await page.locator('.toast', { hasText: 'SISTEMA' }).count() > 0, 'a toast lists the conflicting message');
  await page.keyboard.press('Escape');

  // 4. Precedence: a PROGRAM mapping of the same parameter wins over the PRESET one.
  await post('/api/mappings', { op: 'assign', paramId: 'L1_vol', scope: 'program', midi: { type: 'cc', channel: 16, number: 104 }, takeover: 'jump' });
  const v0 = (await params()).synthLayers[0].volume;
  await cc(102, 5);
  check(Math.abs((await params()).synthLayers[0].volume - v0) < 1e-3, 'PROGRAM shadows the PRESET mapping of the same parameter');
  await cc(104, 127);
  check(Math.abs((await params()).synthLayers[0].volume - 1.5) < 1e-3, 'the PROGRAM mapping drives it');
  await post('/api/mappings', { op: 'remove', paramId: 'L1_vol', scope: 'program' });

  // 5. Preset save / load carries the module mappings; another preset clears them.
  await post('/api/synth/presets/save', { name: TEMP_PRESET, mode: 'new' });
  check((await live()).moduleDirty?.[1] === false, 'saving the preset clears EDITADO');
  await post('/api/synth/factory/load', { id: 2 });
  m = await get('/api/mappings');
  check(!find(m, 'L1_vol', 'module') && !find(m, 'L1_pan', 'module'), 'loading another preset removes the previous preset mappings');
  check(Boolean(find(m, SYS_PARAM, 'system')), 'SYSTEM mappings survive preset changes');
  await post('/api/synth/presets/load', { name: TEMP_PRESET });
  m = await get('/api/mappings');
  check(Boolean(find(m, 'L1_vol', 'module') && find(m, 'L1_pan', 'module')), 'loading the saved preset restores its mappings');

  // 6. MAP mode and SETTINGS overview.
  await open('/synth');
  await page.locator('.map-switch').click();
  check(await page.evaluate(() => document.documentElement.classList.contains('map-mode')), 'MAP mode turns on');
  const chip = await page.evaluate(() => {
    const el = document.querySelector('.knob[data-param="L1_vol"]');
    return el ? getComputedStyle(el, '::after').content : '';
  });
  check(chip.includes('CC 102'), `MAP mode shows the source on the control (${chip})`);
  await page.locator('.map-switch').click();
  await open('/settings');
  const rows = await page.locator('.mapping-list .list__main').allTextContents();
  check(rows.some(r => r.includes('L1_vol') && r.includes('CC 102')) && rows.some(r => r.includes(SYS_PARAM) && r.includes('CC 103')), 'SETTINGS lists PRESET and SYSTEM mappings');
  check(await page.locator('.mapping-conflicts').count() === 1, 'SETTINGS shows the conflict box');
  check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
} finally {
  await browser.close();
  // Remove only what this test created (anything that did not exist before).
  const existed = (paramId, scope) => before.assignments.some(a => a.paramId === paramId && a.scope === scope);
  for (const [paramId, scope] of [['L1_vol', 'module'], ['L1_pan', 'module'], ['L1_vol', 'program'], [SYS_PARAM, 'system']])
    if (!existed(paramId, scope)) await post('/api/mappings', { op: 'remove', paramId, scope }).catch(() => {});
  await post('/api/synth/presets/delete', { name: TEMP_PRESET }).catch(() => {});
  await post('/api/synth/factory/load', { id: originalPreset });
  const after = await get('/api/mappings');
  const sys = a => a.assignments.filter(x => x.scope === 'system').map(x => `${x.paramId}:${x.midi?.number}:${x.midi?.channel}:${x.osc}:${x.takeover}:${x.min}:${x.max}`).sort().join('|');
  check(sys(after) === sys(before), 'SYSTEM mappings are exactly as before the test');
}
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
