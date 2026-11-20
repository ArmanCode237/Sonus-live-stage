// End-to-end smoke test against a running SonusLiveStage (http://localhost:8080).
// Uses the locally installed Chrome/Edge through playwright-core (no download).
//   node scripts/smoke.mjs [baseUrl] [width] [height]
// Every engine value the test changes is restored at the end.
import { chromium } from 'playwright-core';
import { existsSync } from 'node:fs';

const BASE = process.argv[2] ?? 'http://localhost:8080';
const WIDTH = Number(process.argv[3] ?? 1366);
const HEIGHT = Number(process.argv[4] ?? 768);
const BROWSERS = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
];
const executablePath = BROWSERS.find(p => existsSync(p));
if (!executablePath) { console.error('No Chrome/Edge found'); process.exit(2); }

const state = async () => (await fetch(`${BASE}/api/state`)).json();
const setParam = (id, value) => fetch(`${BASE}/api/param`, { method: 'POST', body: JSON.stringify({ id, value }) });
const sleep = ms => new Promise(r => setTimeout(r, ms));

const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };

const browser = await chromium.launch({ executablePath, headless: true });
const page = await browser.newPage({ viewport: { width: WIDTH, height: HEIGHT } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
page.on('console', m => { if (m.type() === 'error' && !m.text().startsWith('Failed to load resource')) errors.push(m.text()); });
page.on('response', r => { if (r.status() >= 400) errors.push(`HTTP ${r.status()} ${r.url()}`); });

// The app polls continuously, so wait for its UI instead of network idle.
const open = async route => {
  await page.goto(BASE + route, { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('.console__head');
  await page.waitForFunction(() => !document.querySelector('.status--offline'));
  await sleep(500);
};
const before = (await state()).params;
const restore = [['pianoLayerVol0', before.pianoLayers[0].volume], ['L1_cut', before.synthLayers[0].cutoffHz], ['drawbar5', before.drawbars[5]]];

try {
  for (const route of ['/', '/piano', '/synth', '/organ', '/fx', '/zones', '/library', '/settings']) {
    await open(route);
    const overflow = await page.evaluate(() => ({
      h: document.scrollingElement.scrollHeight - window.innerHeight,
      w: document.scrollingElement.scrollWidth - window.innerWidth,
    }));
    check(overflow.h <= 0 && overflow.w <= 0, `${route} fits ${WIDTH}x${HEIGHT} without page scroll (${overflow.w}/${overflow.h})`);
    const active = await page.locator('.nav__link.is-active').textContent();
    check(Boolean(active), `${route} highlights its navigation entry (${active})`);
  }

  // Piano: drag the L1 level knob upwards → engine value increases.
  await open('/');
  await setParam('pianoLayerVol0', 0.5); await sleep(300);
  const knob = page.locator('.layer-row').first().locator('.knob__svg');
  // v32.4: the layer row grows when its range / meter arrive: measure once the layout is stable.
  let box = await knob.boundingBox();
  for (let i = 0; i < 10; i++) {
    await sleep(150);
    const next = await knob.boundingBox();
    if (next && box && Math.abs(next.y - box.y) < .5 && Math.abs(next.x - box.x) < .5) break;
    box = next;
  }
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down(); await page.mouse.move(box.x + box.width / 2, box.y - 40, { steps: 8 }); await page.mouse.up();
  await sleep(500);
  check((await state()).params.pianoLayers[0].volume > 0.55, 'Piano: dragging L1 LEVEL writes pianoLayerVol0');

  // MAIN: 8 program pads in two rows of four, plus the three modules.
  const pads = await page.locator('.pad').evaluateAll(els => els.map(e => Math.round(e.getBoundingClientRect().top)));
  check(pads.length === 8 && new Set(pads).size === 2, `MAIN: 8 program pads in 2 rows (${pads.length} pads, ${new Set(pads).size} rows)`);
  check(await page.locator('.piano-module, .synth-mini, .organ-mini').count() === 3, 'MAIN: piano, synth and organ modules present');
  const rotary = (await state()).params.organRotaryFast;
  await page.locator('.rotary-btn').click(); await sleep(500);
  check((await state()).params.organRotaryFast !== rotary, 'MAIN: ROTARY toggles organRotaryFast');
  await setParam('organRotaryFast', rotary ? 1 : 0);

  // Engine → UI: an external change shows up in the knob readout.
  await setParam('pianoLayerVol0', 1.2); await sleep(500);
  const readout = await page.locator('.layer-row').first().locator('.knob__value').textContent();
  check(readout.includes('120'), `Piano: external change is reflected in the UI (${readout})`);

  // Synth: tabs and a filter knob via keyboard.
  await open('/synth');
  const cut = page.locator('.knob', { hasText: 'CUTOFF' }).first().locator('.knob__svg');
  await setParam('L1_cut', 1000); await sleep(300);
  await cut.focus(); for (let i = 0; i < 5; i++) await page.keyboard.press('ArrowUp');
  await sleep(500);
  check((await state()).params.synthLayers[0].cutoffHz > 1000, 'Synth: arrow keys on CUTOFF write L1_cut');
  await page.locator('.patch-bar .btn', { hasText: 'BROWSER' }).click();   // v32.4: sound browser
  await page.waitForSelector('.sb .sb__row');
  await page.locator('.sb__facet--factory').click(); await sleep(200);
  check(await page.locator('.sb__row').count() === 184, 'Synth: factory browser lists 184 patches (96 + 48 vintage + 24 DX7 + 16 LA32)');
  await page.keyboard.press('Escape'); await sleep(200);

  // Organ: click inside the 6th drawbar (2') near its bottom → pulled out.
  await open('/organ');
  await setParam('drawbar5', 0); await sleep(300);
  const bar = await page.locator('.fader__track').nth(5).boundingBox();
  await page.mouse.click(bar.x + bar.width / 2, bar.y + bar.height * 0.95);
  await sleep(500);
  check((await state()).params.drawbars[5] > 0.8, 'Organ: clicking a drawbar writes drawbar5');

  // FX: switch rack and see five slots.
  await open('/fx');
  await page.getByRole('button', { name: 'SYNTH RACK' }).click();
  check(await page.locator('.rack-slot').count() === 5, 'FX: rack shows five insert slots');

  // Piano: the sample browser opens and closes with Escape.
  await open('/');
  await page.locator('.layer-row__name').nth(2).click();
  await page.waitForSelector('.modal');
  // The first open scans the library on the engine side; allow it some time.
  await page.waitForSelector('.sb__row', { timeout: 60000 }).catch(() => {});
  check(await page.locator('.sb__row').count() > 0, 'Piano: sound browser lists library items');
  await page.keyboard.press('Escape');
  check(await page.locator('.modal').count() === 0, 'Piano: Escape closes the browser');

  // Themes: red Stage by default; the header switch selects Studio Dark and it persists.
  await page.evaluate(() => localStorage.removeItem('liveStage.theme'));
  await open('/');
  const themeOf = () => page.evaluate(() => document.documentElement.getAttribute('data-theme'));
  const chassisOf = () => page.evaluate(() => getComputedStyle(document.documentElement).getPropertyValue('--chassis').trim());
  check(await themeOf() === 'stage', `Theme: Stage is the default (${await themeOf()})`);
  const stageChassis = await chassisOf();
  await page.locator('.theme-switch').click();
  check(await themeOf() === 'studio' && await chassisOf() !== stageChassis, `Theme: header switch applies Studio Dark (${stageChassis} → ${await chassisOf()})`);
  await open('/settings');
  check(await themeOf() === 'studio', 'Theme: Studio Dark persists after reloading');
  await page.locator('.theme-switch').click();
  check(await themeOf() === 'stage', 'Theme: switching back restores Stage');
} finally {
  for (const [id, value] of restore) await setParam(id, value);
  await browser.close();
}

check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
