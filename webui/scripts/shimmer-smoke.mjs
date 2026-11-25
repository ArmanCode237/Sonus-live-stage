// End-to-end check of the Shimmer Atmosphere v4 controls in the FX rack.
//   node scripts/shimmer-smoke.mjs [baseUrl]
// Uses an EMPTY slot of the MASTER rack and leaves it empty again afterwards.
import { chromium } from 'playwright-core';
import { existsSync } from 'node:fs';

const BASE = process.argv[2] ?? 'http://localhost:8080';
const executablePath = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/usr/bin/google-chrome'].find(p => existsSync(p));
const RACK = 3;
const racks = async () => (await (await fetch(`${BASE}/api/studio-racks`)).json()).racks;
const studio = body => fetch(`${BASE}/api/studio-racks`, { method: 'POST', body: JSON.stringify(body) });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };

const slot = (await racks())[RACK].slots.findIndex(s => s.type === 0);
if (slot < 0) { console.log('SKIP: no empty slot in the MASTER rack'); process.exit(0); }

const browser = await chromium.launch({ executablePath, headless: true });
const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
const errors = [];
page.on('pageerror', e => errors.push(String(e)));
try {
  await page.goto(`${BASE}/fx`, { waitUntil: 'domcontentloaded' });
  await page.getByRole('button', { name: 'MASTER RACK' }).click();
  const card = page.locator('.rack-slot').nth(slot);
  await card.getByLabel(`Efecto del slot ${slot + 1}`).selectOption('6');
  await sleep(700);
  await card.getByLabel('ALGORITHM').selectOption('3');
  await sleep(1500);

  let s = (await racks())[RACK].slots[slot];
  check(s.type === 6 && s.version === 4, `SHIMMER selected → slot upgraded to Atmosphere v4 (type ${s.type}, v${s.version})`);
  check(s.params[23] < 0.5, 'FREEZE starts released');
  check(Math.abs(s.params[7] - 0.84) < 1e-3 && Math.abs(s.params[4] - 0.8) < 1e-3, 'starting DECAY / SIZE applied');
  const labels = await card.locator('.knob__label').allTextContents();
  for (const name of ['BLOOM', 'PITCH B', 'B LEVEL', 'A LEVEL', 'GRAIN', 'TONE', 'HARM WIDTH', 'DECAY', 'DIFFUSION', 'MOD DEPTH'])
    check(labels.includes(name), `knob ${name} shown`);
  check(await card.getByLabel('PITCH', { exact: true }).count() === 1 && await card.getByLabel('STEREO', { exact: true }).count() === 1 && await card.getByLabel('COLOR', { exact: true }).count() === 1,
    'PITCH engine / STEREO field / COLOR selectors shown');

  if (process.env.SHOT) await card.screenshot({ path: process.env.SHOT });
  // BLOOM knob writes p22 (keyboard).
  const before = s.params[22];
  const bloom = card.locator('.knob', { hasText: 'BLOOM' }).locator('.knob__svg');
  await bloom.focus(); for (let i = 0; i < 10; i++) await page.keyboard.press('ArrowUp');
  await sleep(700);
  s = (await racks())[RACK].slots[slot];
  check(s.params[22] > before, `BLOOM knob writes p22 (${before.toFixed(2)} → ${s.params[22].toFixed(2)})`);

  await card.getByRole('button', { name: 'FREEZE' }).click(); await sleep(600);
  check((await racks())[RACK].slots[slot].params[23] >= 0.5, 'FREEZE on');
  await card.getByRole('button', { name: 'FREEZE' }).click(); await sleep(600);
  check((await racks())[RACK].slots[slot].params[23] < 0.5, 'FREEZE off');

  // Room / Plate / Hall expose their v2 controls.
  await card.getByLabel('ALGORITHM').selectOption('2'); await sleep(900);
  const hall = await card.locator('.knob__label').allTextContents();
  check(['DIFFUSION', 'DECAY', 'WIDTH'].every(n => hall.includes(n)) && !hall.includes('BLOOM'), 'HALL shows DIFFUSION / DECAY / WIDTH, no shimmer controls');
} finally {
  await studio({ op: 'slot', rack: RACK, slot, field: 0, value: 0 });
  await browser.close();
}
check((await racks())[RACK].slots[slot].type === 0, 'slot restored to EMPTY');
check(errors.length === 0, `no JavaScript errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
process.exit(failures.length ? 1 : 0);
