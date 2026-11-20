// v33 MIDI routing / filters end-to-end against a running SonusLiveStage (no MIDI hardware:
// /api/note simulates sources and channels). Uses the module meters (state.modulePeaks).
//  - channel 1 -> piano only, channel 2 -> synth only;
//  - the same key on two channels: releasing one keeps the other sounding;
//  - a note keeps its routing: changing the routing while it is held never leaves it stuck;
//  - the on-screen keyboard (no channel) always plays everything;
//  - Program Change channel filter and CC 120/123 option are stored in /api/system.
// The previous routing and settings are restored at the end.
//   node scripts/routing-smoke.mjs [baseUrl]
const BASE = process.argv[2] ?? 'http://localhost:8080';
const failures = [];
const check = (ok, label) => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}`); if (!ok) failures.push(label); };
const post = async (path, body) => (await fetch(BASE + path, { method: 'POST', body: JSON.stringify(body) })).json();
const get = async path => (await fetch(BASE + path)).json();
const sleep = ms => new Promise(r => setTimeout(r, ms));
const note = (key, on, ch, src = 0) => post('/api/note', { key, vel: 110, on, ch, src });
const PIANO = 0b11111, SYNTH = 0b1111 << 5, ORGAN = 1 << 9;

/** Loudest piano / synth / organ meters over `ms` after an action. */
async function peaks(ms = 450) {
  let p = 0, s = 0, o = 0;
  const end = Date.now() + ms;
  while (Date.now() < end) {
    const st = await get('/api/state');
    const m = st.modulePeaks ?? [0, 0, 0];
    p = Math.max(p, m[0]); s = Math.max(s, m[1]); o = Math.max(o, m[2]);
    await sleep(40);
  }
  return { p, s, o };
}
const quiet = async () => { await post('/api/param', { id: 'panic', value: 1 }); await post('/api/param', { id: 'panic', value: 0 }); await sleep(900); };

const originalRouting = await get('/api/midi/routing');
const originalSystem = await get('/api/system');
const st0 = (await get('/api/state')).params;
const originalPreset = Math.round(st0.synthPresetId);
const restoreParams = { pianoEnabled: st0.pianoEnabled, synthEnabled: st0.synthEnabled, organEnabled: st0.organEnabled };
try {
  await post('/api/param', { id: 'pianoEnabled', value: 1 });
  await post('/api/param', { id: 'synthEnabled', value: 1 });
  await post('/api/param', { id: 'organEnabled', value: 0 });
  await quiet();

  // ---------------------------------------------------------- routing off = as before
  await post('/api/midi/routing', { enabled: false });
  await note(60, true, 1); let pk = await peaks(); await note(60, false, 1);
  check(pk.p > 1e-3 && pk.s > 1e-3, `routing off: channel 1 plays piano and synth (${pk.p.toFixed(3)} / ${pk.s.toFixed(3)})`);
  await quiet();

  // ---------------------------------------------------------- channel 1 -> piano, 2 -> synth
  const r = await post('/api/midi/routing', { enabled: true, rules: [{ source: '*', channel: 1, dest: PIANO }, { source: '*', channel: 2, dest: SYNTH }] });
  check(r.ok && r.enabled && r.rules.length === 2, 'routing stored (2 rules)');
  await note(60, true, 1); pk = await peaks(); await note(60, false, 1);
  check(pk.p > 1e-3 && pk.s < 1e-4, `channel 1 -> piano only (${pk.p.toFixed(3)} / ${pk.s.toFixed(4)})`);
  await quiet();
  await note(62, true, 2); pk = await peaks(); await note(62, false, 2);
  check(pk.s > 1e-3 && pk.p < 1e-4, `channel 2 -> synth only (${pk.p.toFixed(4)} / ${pk.s.toFixed(3)})`);
  await quiet();
  await note(64, true, 3); pk = await peaks(300); await note(64, false, 3);
  check(pk.p < 1e-4 && pk.s < 1e-4, 'channel 3 has no rule -> silent');
  await quiet();

  // ---------------------------------------------------------- same key, two channels
  await post('/api/synth/factory/load', { id: 18 });      // OB-X Warm Pad: sustains while held
  await quiet();
  await note(65, true, 1); await note(65, true, 2);
  const held = await peaks(900);                          // both sounding (pad at full level)
  await note(65, false, 1);
  await sleep(1500);                                      // a released pad would be gone by now
  pk = await peaks(300);
  check(pk.s > held.s * 0.5, `releasing channel 1 keeps channel 2's note (synth ${held.s.toFixed(3)} -> ${pk.s.toFixed(3)})`);
  await note(65, false, 2);
  await quiet();

  // ---------------------------------------------------------- routing change while held
  await note(67, true, 1); await sleep(200);
  await post('/api/midi/routing', { rules: [{ source: '*', channel: 1, dest: SYNTH }] });
  await note(67, false, 1);
  await sleep(2500);
  const voices = (await get('/api/state')).activeVoices;
  check(voices === 0, `note keeps its routing: released after a routing change (voices ${voices})`);

  // ---------------------------------------------------------- on-screen keyboard plays all
  await post('/api/midi/routing', { rules: [{ source: '*', channel: 1, dest: ORGAN }] });
  await post('/api/note', { key: 69, vel: 110, on: true }); pk = await peaks(); await post('/api/note', { key: 69, on: false });
  check(pk.p > 1e-3 && pk.s > 1e-3, 'on-screen keyboard (no channel) plays everything');
  await quiet();

  // ---------------------------------------------------------- M3 sustain pedal per route
  const cc = (ch, n, v) => post('/api/midi/inject', { bytes: [0xB0 + ch - 1, n, v] });
  await post('/api/midi/routing', { enabled: true, pedalFollows: true, rules: [{ source: '*', channel: 1, dest: PIANO }, { source: '*', channel: 2, dest: SYNTH }] });
  await quiet();
  await cc(2, 64, 127);                                   // pedal of the synth keyboard (channel 2)
  await note(72, true, 1); await sleep(250); await note(72, false, 1);
  await sleep(2000);
  let v = (await get('/api/state')).activeVoices;
  check(v === 0, `channel 2's pedal does not hold channel 1's piano note (voices ${v})`);
  await cc(2, 64, 0);
  await cc(1, 64, 127);                                   // pedal of the piano keyboard
  await note(72, true, 1); await sleep(250); await note(72, false, 1);
  await sleep(1200);
  v = (await get('/api/state')).activeVoices;
  check(v > 0, `channel 1's pedal holds its own piano note (voices ${v})`);
  await cc(1, 64, 0);
  await sleep(1500);
  v = (await get('/api/state')).activeVoices;
  check(v === 0, `releasing the pedal releases it (voices ${v})`);
  await post('/api/midi/routing', { pedalFollows: false });
  await cc(2, 64, 127);
  await note(72, true, 1); await sleep(250); await note(72, false, 1);
  await sleep(1200);
  v = (await get('/api/state')).activeVoices;
  check(v > 0, `"PEDAL POR TECLADO" off: any pedal holds everything (voices ${v})`);
  await cc(2, 64, 0);
  await post('/api/midi/routing', { pedalFollows: true });
  await quiet();

  // ---------------------------------------------------------- M4 device-specific mapping
  const port = (await get('/api/midi/routing')).sources.find(x => x.id === 1)?.name;
  if (port) {
    const drive0 = (await get('/api/state')).params.organDrive ?? 0;
    await post('/api/mappings', { op: 'assign', paramId: 'organDrive', scope: 'program', takeover: 'jump', midi: { type: 'cc', channel: 16, number: 105, port } });
    await post('/api/param', { id: 'organDrive', value: 0 });
    await post('/api/mappings', { op: 'inject', midi: { type: 'cc', channel: 16, number: 105, value: 127, src: 2 } });
    const other = (await get('/api/state')).params.organDrive;
    await post('/api/mappings', { op: 'inject', midi: { type: 'cc', channel: 16, number: 105, value: 127, src: 1 } });
    const same = (await get('/api/state')).params.organDrive;
    check(other < 0.01 && same > 0.99, `mapping limited to «${port}» ignores other keyboards (${other} / ${same})`);
    await post('/api/mappings', { op: 'remove', paramId: 'organDrive', scope: 'program' });
    await post('/api/param', { id: 'organDrive', value: drive0 });
  } else console.log('SKIP  device-specific mapping (no MIDI port opened)');

  // ---------------------------------------------------------- M1 filters
  let sys = await post('/api/system', { midiProgramChannel: 10, midiAllNotesOff: false });
  check(sys.midiProgramChannel === 10 && sys.midiAllNotesOff === false, 'Program Change channel and CC 120/123 option stored');
  await post('/api/midi/routing', { enabled: false });
  const slot0 = (await get('/api/state')).activeProgramSlot;
  const other = (slot0 + 1) % 8;
  await post('/api/midi/inject', { bytes: [0xC0, other] });             // channel 1: filtered out
  await sleep(1500);
  check((await get('/api/state')).activeProgramSlot === slot0, 'Program Change on another channel is ignored');
  await post('/api/midi/inject', { bytes: [0xC9, other] });             // channel 10: accepted
  for (let i = 0; i < 40 && (await get('/api/state')).activeProgramSlot !== other; i++) await sleep(150);
  check((await get('/api/state')).activeProgramSlot === other, 'Program Change on the chosen channel recalls the program');
  await post('/api/midi/inject', { bytes: [0xC9, slot0] });             // back to where we were
  for (let i = 0; i < 40 && (await get('/api/state')).activeProgramSlot !== slot0; i++) await sleep(150);
  await sleep(800);
  await note(60, true, 1); await sleep(200);
  await post('/api/midi/inject', { bytes: [0xB0, 123, 0] });            // all notes off: ignored now
  await sleep(300);
  const heldVoices = (await get('/api/state')).activeVoices;
  check(heldVoices > 0, `CC 123 ignored while the option is off (voices ${heldVoices})`);
  await post('/api/system', { midiAllNotesOff: true });
  await post('/api/midi/inject', { bytes: [0xB0, 123, 0] });
  await sleep(600);
  check((await get('/api/state')).activeVoices === 0, 'CC 123 cuts the sound when the option is on (MIDI standard)');
  await note(60, false, 1);
  const rt = await get('/api/midi/routing');
  check(rt.sources?.[0]?.name === 'internal' && rt.destinations?.length === 10, `sources listed (${rt.sources.map(s => `${s.id}:${s.name}`).join(', ')})`);
} finally {
  await post('/api/midi/routing', { enabled: originalRouting.enabled, rules: originalRouting.rules, pedalFollows: originalRouting.pedalFollows ?? true });
  await post('/api/synth/factory/load', { id: originalPreset });
  await post('/api/system', { midiProgramChannel: originalSystem.midiProgramChannel ?? 0, midiAllNotesOff: originalSystem.midiAllNotesOff ?? true });
  for (const [id, v] of Object.entries(restoreParams)) await post('/api/param', { id, value: v ? 1 : 0 });
  await quiet();
}
console.log(failures.length ? `\n${failures.length} FAILED` : '\nALL PASS');
process.exit(failures.length ? 1 : 0);
