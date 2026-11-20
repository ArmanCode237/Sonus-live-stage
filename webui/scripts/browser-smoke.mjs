// Sound browser (v32.4) end-to-end check against a running SonusLiveStage:
//  - PERFORM synth BROWSER lists USER presets (regression: it only listed the factory bank);
//  - origin facet, search, tags (stored in browser_meta.json) and tag filter;
//  - double click loads a user preset and PERFORM's synth LCD shows its name;
//  - organ (PERFORM + ORGAN page), piano (PERFORM) and SYNTH page browsers open on top of everything;
//  - DX7 cartridges dialog stacks over the browser and Escape closes only the top dialog.
// Everything it creates (user preset, tags, usage) is removed at the end.
//   node scripts/browser-smoke.mjs [baseUrl] [screenshotDir]
import { chromium } from 'playwright-core';
import { existsSync, mkdirSync } from 'node:fs';

const BASE = process.argv[2] ?? 'http://localhost:8080';
const SHOTS = process.argv[3] ?? null;
const BROWSERS = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
];
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };
const post = async (path, body) => (await fetch(BASE + path, { method: 'POST', body: JSON.stringify(body) })).json();
const get = async path => (await fetch(BASE + path)).json();
const sleep = ms => new Promise(r => setTimeout(r, ms));

const NAME = 'zz smoke browser preset';
const KEY = `synth:u:${NAME}`;
const TAG = 'zzsmoketag';
const originalPreset = Math.round((await get('/api/state')).params.synthPresetId);
if ((await get('/api/synth/presets')).some(p => p.name === NAME)) { console.error(`«${NAME}» already exists`); process.exit(2); }
await post('/api/synth/presets/save', { name: NAME });

if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const browser = await chromium.launch({ executablePath: BROWSERS.find(b => existsSync(b)), headless: true });
const page = await browser.newPage({ viewport: { width: 1366, height: 768 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
const open = async route => {
  await page.goto(BASE + route, { waitUntil: 'domcontentloaded' });
  await page.waitForSelector('.console__head');
  await sleep(700);
};
const shot = async name => { if (SHOTS) await page.screenshot({ path: `${SHOTS}/${name}.png` }); };
/** The browser window is on top: the element at its centre belongs to the dialog. */
const onTop = () => page.evaluate(() => {
  const w = [...document.querySelectorAll('.modal__window')].pop();
  if (!w) return false;
  const r = w.getBoundingClientRect();
  return [[0.5, 0.5], [0.1, 0.1], [0.9, 0.9]].every(([x, y]) => w.contains(document.elementFromPoint(r.left + r.width * x, r.top + r.height * y)));
});
const rows = () => page.locator('.sb__row');

try {
  // ------------------------------------------------ PERFORM · synth
  await open('/');
  await page.locator('.synth-mini .btn', { hasText: 'BROWSER' }).click();
  await page.waitForSelector('.sb .sb__row');
  check(await onTop(), 'PERFORM synth browser is on top');
  const total = await rows().count();
  check(total >= 184, `PERFORM synth browser lists the whole bank (${total} rows)`);
  await page.locator('.sb__facet--user').click(); await sleep(200);
  const userNames = await page.locator('.sb__row .sb__name').allTextContents();
  check(userNames.includes(NAME), `USUARIO facet lists the new user preset (${userNames.length} user presets)`);
  await shot('perform-synth-user');

  await page.locator('.sb__clear').click();
  await page.locator('.sb__search').fill('zz smoke browser');
  await sleep(200);
  check(await rows().count() === 1, 'search finds the user preset');
  await rows().first().click();
  await page.locator('.sb__tagadd input').fill(TAG);
  await page.locator('.sb__tagadd input').press('Enter');
  await sleep(500);
  check((await get('/api/browser/meta')).items?.[KEY]?.tags?.includes(TAG), 'tag stored in browser_meta.json');
  await page.locator('.sb__search').fill('');
  await page.locator('.sb__chip', { hasText: `#${TAG}` }).first().click(); await sleep(200);
  check(await rows().count() === 1, 'tag chip filters to the tagged preset');
  await shot('perform-synth-tag');

  await rows().first().dblclick();
  await sleep(900);
  check(((await get('/api/browser/meta')).usage?.[KEY]?.count ?? 0) >= 1, 'loading records the use (recents / most used)');
  check(await rows().first().evaluate(e => e.classList.contains('is-active')), 'loaded preset is marked active');
  await page.keyboard.press('Escape'); await sleep(300);
  check(await page.locator('.sb').count() === 0, 'Escape closes the browser');
  const lcd = await page.locator('.synth-mini .mini-lcd__name').evaluate(e => e.textContent + ' ' + (e.querySelector('[aria-label]')?.getAttribute('aria-label') ?? ''));
  check(lcd.includes(NAME), 'PERFORM synth LCD shows the user preset name');

  // ------------------------------------------------ PERFORM · organ + piano
  await page.locator('.organ-mini .btn', { hasText: 'BROWSER' }).click();
  await page.waitForSelector('.sb .sb__row');
  check(await onTop() && await rows().count() > 0, `PERFORM organ browser opens on top (${await rows().count()} rows)`);
  await shot('perform-organ');
  await page.keyboard.press('Escape'); await sleep(300);

  await page.locator('.btn', { hasText: 'BROWSER' }).first().click();   // piano sound bank
  await page.waitForSelector('.sb');
  await sleep(800);
  check(await onTop(), `PERFORM piano browser opens on top (${await rows().count()} rows)`);
  check((await page.locator('.modal__head h2').textContent()).includes('PIANO'), 'first BROWSER on PERFORM is the piano one');
  await shot('perform-piano');
  await page.keyboard.press('Escape'); await sleep(300);

  // ------------------------------------------------ SYNTH page + DX7 dialog
  await open('/synth');
  await page.locator('.patch-bar .btn', { hasText: 'BROWSER' }).click();
  await page.waitForSelector('.sb .sb__row');
  check(await onTop(), 'SYNTH page browser is on top');
  await page.locator('.sb__view[title="Tarjetas"]').click(); await sleep(200);
  await shot('synth-cards');
  await page.locator('.sb__view[title="Por categoría"]').click();
  await page.locator('.sb__extra .sb__facet', { hasText: 'DX7' }).click(); await sleep(400);
  check(await page.locator('.modal').count() === 2 && await onTop(), 'DX7 cartridges dialog stacks over the browser');
  await page.keyboard.press('Escape'); await sleep(300);
  check(await page.locator('.modal').count() === 1 && await page.locator('.sb').count() === 1, 'Escape closes only the top dialog');
  await page.keyboard.press('Escape'); await sleep(300);

  // ------------------------------------------------ ORGAN page
  await open('/organ');
  await page.locator('.organ-model .btn', { hasText: 'BROWSER' }).click();
  await page.waitForSelector('.sb .sb__row');
  check(await onTop(), 'ORGAN page browser is on top');
  await page.keyboard.press('Escape');

  // ------------------------------------------------ ACERCA DE (logo)
  await page.locator('.brand').click();
  await page.waitForSelector('.about');
  check((await page.locator('.modal__head h2').textContent()).includes('ACERCA DE'), 'the logo opens ACERCA DE');
  const about = await get('/api/about');
  check(/^\d+\.\d+\.\d+/.test(about.version), `version shown (${about.version})`);
  const support = page.locator('.about__support-btn');
  check(await support.isDisabled() === !about.links.support, 'support button follows config/links.json');
  await shot('about');
  await page.keyboard.press('Escape');
  check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
} finally {
  await browser.close();
  await post('/api/synth/presets/delete', { name: NAME });
  await post('/api/browser/meta', { op: 'forget', key: KEY });
  await post('/api/synth/factory/load', { id: originalPreset }).catch(() => {});
}
console.log(failures.length ? `\n${failures.length} FAILED` : '\nALL PASS');
process.exit(failures.length ? 1 : 0);
