// Protocol check for the real-time channel (/ws) against a running engine.
//   node scripts/ws-smoke.mjs [host:port]
// Restores every parameter it changes.
const HOST = process.argv[2] ?? 'localhost:8080';
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };
const sleep = ms => new Promise(r => setTimeout(r, ms));

function open() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://${HOST}/ws`);
    const frames = [];
    const waiters = [];
    ws.onmessage = e => {
      const f = JSON.parse(e.data); f.at = performance.now(); frames.push(f);
      for (const w of waiters.slice()) if (w.test(f)) { waiters.splice(waiters.indexOf(w), 1); w.resolve(f); }
    };
    ws.onopen = () => resolve({ ws, frames, next: (test, ms = 2000) => new Promise((res, rej) => {
      const found = frames.find(test); if (found) return res(found);
      const w = { test, resolve: res }; waiters.push(w);
      setTimeout(() => { const i = waiters.indexOf(w); if (i >= 0) { waiters.splice(i, 1); rej(new Error('timeout')); } }, ms);
    }) });
    ws.onerror = () => reject(new Error('cannot connect'));
  });
}

const c = await open();
const snap = await c.next(f => f.t === 'snapshot');
check(snap.params && snap.state && typeof snap.seq === 'number', 'snapshot with state + params on connect');
const original = snap.params.synthLayers[0].cutoffHz;

// Write → ack → patch (round trip measured from send to the patch reflecting it).
const target = original === 1234 ? 1500 : 1234;
const t0 = performance.now();
c.ws.send(JSON.stringify({ op: 'set', id: 'L1_cut', v: target, seq: 1 }));
const ack = await c.next(f => f.t === 'ack' && f.seq === 1);
const tAck = performance.now();
const patch = await c.next(f => f.t === 'patch' && f.ops.some(o => o.path === '/synthLayers/0/cutoffHz' && Math.abs(o.value - target) < 0.5));
check(ack.ok === true, `set acknowledged in ${(tAck - t0).toFixed(1)} ms`);
check(Boolean(patch), `change published as RFC 6902 patch in ${(patch.at - t0).toFixed(1)} ms (seq ${patch.seq})`);

// Frame rate of live state.
const before = c.frames.filter(f => f.t === 'state').length;
await sleep(2000);
const rate = (c.frames.filter(f => f.t === 'state').length - before) / 2;
check(rate >= 20 && rate <= 40, `live state at ~${rate.toFixed(0)} frames/s`);
const last = c.frames.filter(f => f.t === 'state').at(-1);
check(last && 'peakL' in last && 'activeVoices' in last && !('params' in last), 'live frames carry meters/voices, not the edit buffer');

// Unknown parameter is rejected, not crashing the link.
c.ws.send(JSON.stringify({ op: 'set', id: 'noSuchParam', v: 1, seq: 2 }));
check((await c.next(f => f.t === 'ack' && f.seq === 2)).ok === false, 'unknown parameter → ack ok:false');

// Client cap: 4 connections; a 5th is closed by the server.
const extra = [];
for (let i = 0; i < 3; i++) extra.push(await open());
const fifth = await new Promise(resolve => {
  const ws = new WebSocket(`ws://${HOST}/ws`);
  ws.onclose = () => resolve('closed'); ws.onmessage = () => resolve('served');
  setTimeout(() => resolve('timeout'), 3000);
});
check(fifth === 'closed', `5th client refused (${fifth})`);
for (const x of extra) x.ws.close();

// Restore.
c.ws.send(JSON.stringify({ op: 'set', id: 'L1_cut', v: original, seq: 3 }));
await c.next(f => f.t === 'ack' && f.seq === 3);
c.ws.close();
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nall checks passed');
await sleep(300); // let sockets finish closing (avoids a libuv assert on Windows)
process.exit(failures.length ? 1 : 0);
