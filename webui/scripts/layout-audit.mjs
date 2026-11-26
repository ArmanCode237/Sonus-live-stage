// Layout audit against a running SonusLiveStage (http://localhost:8080).
// For every route × viewport × theme it reports:
//   - page overflow (the document itself must never scroll);
//   - clipped controls: buttons/inputs/sliders cut off by an ancestor that cannot scroll to them;
//   - navigation tabs that are not fully visible;
//   - covered dialogs: a modal must be the topmost element at its centre and corners.
//   node scripts/layout-audit.mjs [baseUrl] [--shots dir] [--routes /,/piano] [--sizes 1366x768] [--themes stage]
// Exit code 1 when anything fails. Read-only: it changes no engine state.
import { chromium } from 'playwright-core';
import { existsSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';

const args = process.argv.slice(2);
const opt = name => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; };
const SHOTS = opt('--shots');
const BASE = args.find((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1].startsWith('--'))) ?? 'http://localhost:8080';
const ROUTES = opt('--routes')?.split(',') ?? ['/', '/piano', '/synth', '/organ', '/fx', '/zones', '/library', '/settings'];
const VIEWPORTS = opt('--sizes')?.split(',').map(v => v.split('x').map(Number)) ?? [[1280, 720], [1366, 768], [1600, 900], [1920, 1080]];
const THEMES = opt('--themes')?.split(',') ?? ['stage', 'studio'];
const BROWSERS = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
];
const executablePath = BROWSERS.find(p => existsSync(p));
if (!executablePath) { console.error('No Chrome/Edge found'); process.exit(2); }
if (SHOTS) mkdirSync(SHOTS, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));

// Runs in the page: returns the problems found in the current view.
function inspect() {
  const TOL = 1;
  const out = { overflow: null, clipped: [], hiddenTabs: [] };
  const se = document.scrollingElement;
  const ow = se.scrollWidth - window.innerWidth, oh = se.scrollHeight - window.innerHeight;
  if (ow > TOL || oh > TOL) out.overflow = `${ow}x${oh}`;
  const cls = el => String(el.getAttribute('class') || el.tagName).split(' ')[0];
  const label = el => (el.getAttribute('aria-label') || el.textContent || el.getAttribute('placeholder') || cls(el))
    .toString().trim().replace(/\s+/g, ' ').slice(0, 40);
  const where = el => { const p = el.closest('.panel, .console__head, .modal, header, footer'); return p ? (p.querySelector('.panel__title')?.textContent || cls(p)) : '?'; };
  const controls = document.querySelectorAll('button, input:not([type=hidden]), select, textarea, [role=slider], a.nav__link');
  for (const el of controls) {
    const r = el.getBoundingClientRect();
    if (r.width < 2 || r.height < 2) continue;
    const cs = getComputedStyle(el);
    if (cs.visibility === 'hidden') continue;
    let clippedBy = null, reachable = false;
    for (let a = el.parentElement; a && a !== document.documentElement; a = a.parentElement) {
      const s = getComputedStyle(a);
      if (s.overflowX === 'visible' && s.overflowY === 'visible') continue;
      const ar = a.getBoundingClientRect();
      // With CSS zoom, getBoundingClientRect is in zoomed px but clientWidth/Height are not.
      const sx = a.offsetWidth ? ar.width / a.offsetWidth : 1, sy = a.offsetHeight ? ar.height / a.offsetHeight : 1;
      const left = ar.left + a.clientLeft * sx, top = ar.top + a.clientTop * sy;
      const outX = r.left < left - TOL || r.right > left + a.clientWidth * sx + TOL;
      const outY = r.top < top - TOL || r.bottom > top + a.clientHeight * sy + TOL;
      const scrollX = /(auto|scroll)/.test(s.overflowX) && a.scrollWidth > a.clientWidth;
      const scrollY = /(auto|scroll)/.test(s.overflowY) && a.scrollHeight > a.clientHeight;
      // A scrollable ancestor makes the control reachable; it is checked from there on.
      if ((outX && !scrollX && s.overflowX !== 'visible') || (outY && !scrollY && s.overflowY !== 'visible')) { clippedBy = a; break; }
      if ((outX && scrollX) || (outY && scrollY)) { reachable = true; break; }
    }
    if (!clippedBy && !reachable && (r.right > window.innerWidth + TOL || r.bottom > window.innerHeight + TOL || r.left < -TOL || r.top < -TOL)) clippedBy = document.body;
    if (clippedBy) out.clipped.push(`${where(el)} › ${label(el)} (by .${cls(clippedBy)})`);
  }
  for (const a of document.querySelectorAll('.nav__link')) {
    const r = a.getBoundingClientRect();
    const n = a.closest('.nav').getBoundingClientRect();
    if (r.left < n.left - TOL || r.right > n.right + TOL || r.right > window.innerWidth || r.width < 8) out.hiddenTabs.push(label(a));
  }
  return out;
}

function modalOnTop() {
  const w = document.querySelector('.modal__window');
  if (!w) return 'no modal';
  const r = w.getBoundingClientRect();
  const pts = [[r.left + r.width / 2, r.top + r.height / 2], [r.left + 6, r.top + 6], [r.right - 6, r.top + 6], [r.left + 6, r.bottom - 6], [r.right - 6, r.bottom - 6]];
  for (const [x, y] of pts) {
    if (x < 0 || y < 0 || x > innerWidth || y > innerHeight) return `outside viewport at ${Math.round(x)},${Math.round(y)}`;
    const hit = document.elementFromPoint(x, y);
    if (!hit || !hit.closest('.modal')) return `covered at ${Math.round(x)},${Math.round(y)} by .${hit?.getAttribute('class')}`;
  }
  return null;
}

const browser = await chromium.launch({ executablePath, headless: true });
const failures = [];
let checks = 0;
const report = (ok, msg) => { checks++; if (!ok) { failures.push(msg); console.log(`FAIL  ${msg}`); } };

try {
  for (const theme of THEMES) {
    for (const [w, h] of VIEWPORTS) {
      const ctx = await browser.newContext({ viewport: { width: w, height: h } });
      await ctx.addInitScript(t => { try { localStorage.setItem('liveStage.theme', t); localStorage.setItem('liveStage.keyboard', '0'); } catch { /* ignore */ } }, theme);
      const page = await ctx.newPage();
      for (const route of ROUTES) {
        await page.goto(BASE + route, { waitUntil: 'domcontentloaded' });
        await page.waitForSelector('.console__head');
        await page.waitForFunction(() => !document.querySelector('.status--offline'), null, { timeout: 8000 }).catch(() => {});
        await sleep(450);
        const tag = `[${theme} ${w}x${h} ${route}]`;
        const applied = await page.evaluate(() => document.documentElement.getAttribute('data-theme'));
        report(applied === theme, `${tag} data-theme is "${applied}"`);
        const r = await page.evaluate(inspect);
        report(!r.overflow, `${tag} page overflow ${r.overflow}`);
        report(r.hiddenTabs.length === 0, `${tag} hidden tabs: ${r.hiddenTabs.join(', ')}`);
        report(r.clipped.length === 0, `${tag} ${r.clipped.length} clipped controls: ${r.clipped.slice(0, 6).join(' | ')}`);
        if (SHOTS) await page.screenshot({ path: join(SHOTS, `${theme}-${w}x${h}${route === '/' ? '-main' : route.replace('/', '-')}.png`) });

        if (route === '/') {
          // Instrument browser over MAIN.
          const browse = page.locator('.piano-module button', { hasText: 'BROWSER' }).first();
          if (await browse.count()) {
            await browse.click(); await page.waitForSelector('.modal__window'); await sleep(250);
            report(!(await page.evaluate(modalOnTop)), `${tag} instrument browser: ${await page.evaluate(modalOnTop)}`);
            if (SHOTS) await page.screenshot({ path: join(SHOTS, `${theme}-${w}x${h}-main-browser.png`) });
            await page.keyboard.press('Escape'); await sleep(150);
          }
          // STORE dialog: STORE then a pad, cancelled without saving.
          const store = page.locator('.programs .btn--primary').first();
          if (await store.count()) {
            await store.click(); await page.locator('.pad').nth(7).click();
            const opened = await page.waitForSelector('.modal__window', { timeout: 1500 }).then(() => true).catch(() => false);
            if (opened) {
              await sleep(200);
              report(!(await page.evaluate(modalOnTop)), `${tag} STORE dialog: ${await page.evaluate(modalOnTop)}`);
              await page.keyboard.press('Escape'); await sleep(150);
            }
            if (await page.locator('.pads.is-storing').count()) await store.click({ force: true });
          }
        }
      }
      await ctx.close();
    }
  }
} finally {
  await browser.close();
}
console.log(`\n${checks - failures.length}/${checks} checks passed`);
process.exit(failures.length ? 1 : 0);
