// v32.4 soak test (run before a tour): plays chords non-stop and recalls the next of the
// 8 programs every 30 s, against a running SonusLiveStage. Reports audio dropouts,
// slow blocks, dropped MIDI, DSP peak and sample RAM; restores the starting program.
//   npm run soak            -> 60 minutes
//   npm run soak -- 5       -> 5 minutes
const BASE = process.env.STAGE_URL ?? 'http://localhost:8080';
const minutes = Number(process.argv[2] ?? 60);
const post = async (p, b) => (await fetch(BASE + p, { method: 'POST', body: JSON.stringify(b ?? {}) })).json();
const get = async p => (await fetch(BASE + p)).json();
const sleep = ms => new Promise(r => setTimeout(r, ms));
const stamp = () => new Date().toLocaleTimeString();

const start = await get('/api/state');
const slot0 = start.activeProgramSlot;
await post('/api/audio/counters/reset');
const t0 = Date.now(), end = t0 + minutes * 60_000;
let recalls = 0, maxDsp = 0, maxRam = 0, maxVoices = 0, errors = 0, lastSlot = slot0;
const chords = [[48, 55, 60, 64, 67], [45, 52, 57, 60, 64], [41, 48, 53, 57, 60], [43, 50, 55, 59, 62]];
console.log(`[${stamp()}] soak ${minutes} min against ${BASE} (program ${slot0 + 1} restored at the end)`);
try {
  let nextRecall = Date.now() + 30_000, chord = 0;
  while (Date.now() < end) {
    try {
      const notes = chords[chord++ % chords.length];
      for (const k of notes) await post('/api/note', { key: k, on: true, vel: 70 + Math.floor(Math.random() * 50) });
      await sleep(900);
      for (const k of notes) await post('/api/note', { key: k, on: false });
      await sleep(250);
      const s = await get('/api/state');
      maxDsp = Math.max(maxDsp, s.dspPeak ?? 0);
      maxVoices = Math.max(maxVoices, s.activeVoices ?? 0);
      if (Date.now() >= nextRecall) {
        lastSlot = (lastSlot + 1) % 8;
        await post('/api/programs/recall', { slot: lastSlot });
        recalls++;
        nextRecall = Date.now() + 30_000;
        const sys = await get('/api/system');
        maxRam = Math.max(maxRam, sys.sampleResidentMB ?? 0);
        console.log(`[${stamp()}] program ${lastSlot + 1} · XRUN ${s.audioUnderflows} · slow ${s.audioOverBudgetCallbacks} · MIDI lost ${s.midiDropped ?? 0} · DSP peak ${Math.round(maxDsp * 100)} % · RAM ${Math.round(sys.sampleResidentMB)} MB`);
      }
    } catch (e) { errors++; console.log(`[${stamp()}] request error: ${e.message}`); await sleep(1000); }
  }
} finally {
  await post('/api/panic').catch(() => {});
  await post('/api/programs/recall', { slot: slot0 }).catch(() => {});
}
const s = await get('/api/state');
const fail = s.audioUnderflows > 0 || (s.midiDropped ?? 0) > 0 || errors > 0;
console.log(`\n${fail ? 'FAIL' : 'PASS'}  ${minutes} min · ${recalls} program recalls · peak voices ${maxVoices} · XRUN ${s.audioUnderflows} · slow blocks ${s.audioOverBudgetCallbacks} · MIDI lost ${s.midiDropped ?? 0} · DSP peak ${Math.round(maxDsp * 100)} % · sample RAM peak ${Math.round(maxRam)} MB · request errors ${errors}`);
process.exit(fail ? 1 : 0);
