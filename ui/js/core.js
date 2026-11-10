// ============================================================================
// ui/js/core.js — Núcleo compartido por TODAS las páginas (index, synth, organ)
//   · constantes y estado global (`state`)
//   · comunicación con C++ (sendParam)
//   · utilidades pequeñas: setText/setActive, modales, navegación entre páginas
// Debe cargarse PRIMERO. No toca el DOM al cargar (eso lo hace app.js).
// ============================================================================

const DEFAULT_FREQS = [60, 250, 1000, 4000, 12000];
const BAND_NAMES = ['BAND 1 (LOW)', 'BAND 2 (LOW-MID)', 'BAND 3 (MID)', 'BAND 4 (HI-MID)', 'BAND 5 (HIGH)'];
const SOUND_GROUPS = [
  'Acoustic Pianos', 'Electric Pianos', 'Accordion & Organ',
  'Xylophone & Mallets', 'Strings, Brass & Pads', 'Synth & Misc Samples',
  'Pads & Atmospheres', 'Synth Leads', 'Bass & Sub', 'Plucks & Arpeggios',
  'Bells & Chimes', 'Brass', 'Strings', 'Choirs & Vocals', 'Guitars & Plucked',
  'Drums & Percussion', 'Ambient & Textures', 'Sound Effects',
  'Upright & Felt Pianos', 'Clavinet', 'Harpsichord', 'Celesta', 'Music Box',
  'Orchestral Hits', 'Woodwinds', 'Flutes', 'Reeds', 'World & Ethnic',
  'Analog Synth', 'Digital Synth', 'FM Keys', 'Synth Arps', 'Synth Sequences',
  'Cinematic Drones', 'Soundscapes', 'Pads & Motion', 'Bass Acoustic',
  'Bass Electric', 'Bass Synth', 'Leads Mono', 'Leads Poly', 'Plucked Synth',
  'Percussive Keys', 'Glockenspiel', 'Vibraphone', 'Marimba', 'Programs'
];
const SYNTH_PRESET_NAMES = ["", "Juno Warm Pad", "Jupiter Brass", "Jupiter Lead", "Crystal Pluck", "Juno Strings", "Deep Analog Bass", "PWM Motion", "Jupiter Poly", "Analog Keys", "Glass Bells", "Motion Pad", "Vintage Brass", "Neo Poly Keys", "Dyno Analog", "Soft Tines", "Juno Comp", "Air EP", "Wide Spark", "Velvet Cloud", "Slow PWM", "Warm Ensemble", "Atmosphere", "Dark Cinema", "Poly Horizon", "Classic Poly Brass", "Jupiter Stab", "Soft Horns", "Bright Brass", "Synth Section", "Power Brass", "Rubber Bass", "Sub Foundation", "DCO Punch", "Acid Pulse", "Deep Stack", "Grit Bass", "Mono Vector", "Pulse Solo", "Classic Pro", "Octave Lead", "Resonant Edge", "Bright Solo", "Stereo Saw", "Hyper Poly", "Wide 80s", "Unison Rise", "Air Saw", "Stadium Saw"];
const SYNTH_PRESET_CATEGORIES = ["", "PADS", "BRASS", "LEADS", "KEYS", "STRINGS", "BASS", "PADS", "KEYS", "KEYS", "BELLS", "PADS", "BRASS", "KEYS", "KEYS", "KEYS", "KEYS", "KEYS", "KEYS", "PADS", "PADS", "PADS", "PADS", "PADS", "PADS", "BRASS", "BRASS", "BRASS", "BRASS", "BRASS", "BRASS", "BASS", "BASS", "BASS", "BASS", "BASS", "BASS", "LEADS", "LEADS", "LEADS", "LEADS", "LEADS", "LEADS", "SAWS", "SAWS", "SAWS", "SAWS", "SAWS", "SAWS"];

const state = {
  pianoEnabled: true, pianoVolume: 1.0,
  activePianoLayer: 0,
  pianoLayers: Array.from({length:5},(_,i)=>({enabled:i===0,volume:i===0?1:0.75})),
  pianoLayerSources: Array.from({length:5},(_,i)=>({name:i===0?'Sin sonido':'VACÍA',sourcePath:''})),
  pianoSampleConfig: Array.from({length:5},()=>({attackSec:0.005,decaySec:3.5,sustainLevel:1,releaseSec:.35,loopHold:.65,pan:0,fineTuneCents:0,cutoffHz:20000,resonanceQ:0.707})),
  fxSends: Array.from({length:9},()=>({modulation:1,delay:1,reverb:1})),
  tune: 0, volume: 0.85, pan: 0.0,
  transpose: 0, octave: 0, touch: 1, softRelease: false,
  cutoff: 20000, resonance: 0.71,
  attack: 0.001, decay: 3.50, sustain: 1.00, release: 0.35, loopHold: 0.65,
  eqEnabled: true, lowCut: 20, highCut: 20000,
  eqFreq0: 60, eqFreq1: 250, eqFreq2: 1000, eqFreq3: 4000, eqFreq4: 12000,
  eqQ0: 0.9, eqQ1: 1.0, eqQ2: 1.0, eqQ3: 1.0, eqQ4: 0.9,
  eqGain0: 0, eqGain1: 0, eqGain2: 0, eqGain3: 0, eqGain4: 0,
  chorusEnabled: false, chorusRate: 0.8, chorusDepth: 0.45, chorusMix: 0.35, chorusVoices:2, chorusWidth:.85, chorusFeedback:.08, chorusTone:9500, chorusDelay:16,
  phaserEnabled: false, phaserRate: 0.6, phaserDepth: 0.60, phaserFeedback: 0.40, phaserMix: 0.35,
  tremoloEnabled: false, tremoloRate: 4.5, tremoloDepth: 0.50,
  delayEnabled: false, delayTimeMs: 360, delayFeedback: 0.35, delayDamping: 0.35, delayMix: 0.25, delayPingPong: true,
  reverbEnabled: true, reverbType: 2, reverbMix: 0.25, reverbSize: 0.65, reverbDamping: 0.40, reverbPreDelayMs: 18, reverbShimmer: 0.45,
  shimmerShiftSt: 12, shimmerFeedback: 0.65, shimmerDiffusion: 0.88,
  shimmerLowCutHz: 140, shimmerHighCutHz: 8500,
  shimmerModRateHz: 0.45, shimmerModDepth: 0.25,
  shimmerPitchMode: 1, shimmerReverbMode: 2, shimmerColorMode: 1,
  synthEnabled: false, synthPresetId: 1, synthMasterVol: 0.80, synthMacroCutoff: 1.0,
  synthLfoEnabled: false, synthLfoWave: 0, synthLfoRate: 4.5,
  synthLfoDepth: 0, synthLfoPitch: 0, synthLfoCutoff: 0,
  organEnabled: false, organModel: 0, organVolume: 0.80, organRotaryFast: false, organDrive: 0.12,
  organRotaryMix: 1, organVibratoDepth: 1, organVibratoRate: 6.8,
  organChorusEnabled:false,organChorusRate:.65,organChorusDepth:.55,organChorusMix:.3,organChorusWidth:.85,organChorusFeedback:.08,organChorusTone:9000,organChorusVoices:3,
  organVibratoMode: 0, organPercEnabled: false, organPercHarmonic: 3, organPercDecaySec: 0.35, organPercLevel: 0.85,
  activeModTab: 'chorus', storeMode: false, activeProgramSlot: 0,
  programs: [], libraryItems: [], filteredList: [],
  selectedCategory: 'ALL', currentSourcePath: '', currentInstId: -1,
  lastParamRev: 0, isDraggingControl: false,
  keyboardVisible: localStorage.getItem('nord_kb_vis') !== '0',
  mousePressedNote: -1
};

// Inicializar valores por defecto de las 3 capas del Synth y 9 drawbars
for (let i = 1; i <= 3; i++) {
  state[`L${i}_en`] = (i <= 2);
  state[`L${i}_wave`] = (i === 1 ? 4 : (i === 2 ? 0 : 3));
  state[`L${i}_vol`] = 0.75;
  state[`L${i}_pan`] = 0.0;
  state[`L${i}_oct`] = 0;
  state[`L${i}_semi`] = 0;
  state[`L${i}_det`] = 0;
  state[`L${i}_cut`] = 8500;
  state[`L${i}_res`] = 0.85;
  state[`L${i}_atk`] = 0.015;
  state[`L${i}_dec`] = 1.2;
  state[`L${i}_sus`] = 0.80;
  state[`L${i}_rel`] = 0.45;
  state[`L${i}_unison`] = 1;
  state[`L${i}_uniDet`] = 10.0;
  state[`L${i}_uniSprd`] = 0.50;
  state[`L${i}_filtType`] = 0;
  state[`L${i}_filtEnv`] = 0.0;
  state[`L${i}_fAtk`] = 0.01;
  state[`L${i}_fDec`] = 0.40;
  state[`L${i}_fSus`] = 0.50;
  state[`L${i}_fRel`] = 0.40;
  state[`L${i}_saw`] = 0.85;
  state[`L${i}_pulse`] = 0.40;
  state[`L${i}_pw`] = 0.50;
  state[`L${i}_pwmDepth`] = 0.0;
  state[`L${i}_pwmRate`] = 0.55;
  state[`L${i}_sub`] = 0.25;
  state[`L${i}_noise`] = 0.02;
  state[`L${i}_osc2`] = 0.0;
  state[`L${i}_osc2Det`] = 7.0;
  state[`L${i}_osc2Semi`] = 0;
  state[`L${i}_osc2Wave`] = 0;
  state[`L${i}_hpf`] = 20;
  state[`L${i}_drive`] = 0.0;
  state[`L${i}_filtKT`] = 0.5;
  state[`L${i}_velCut`] = 0.2;
  state[`L${i}_wtIdx`] = 0;
}
[1, 1, 1, 0, 0, 0, 0, 0, 0].forEach((v, d) => { state[`drawbar${d}`] = v; });

// ============================================================================

// ----------------------------------------------------------------------------
// Utilidades DOM seguras: las páginas no comparten todos los elementos,
// por eso NUNCA se accede con getElementById(...).prop sin comprobar.
// ----------------------------------------------------------------------------
function $(id) { return document.getElementById(id); }
function setText(id, text) { const el = $(id); if (el) el.innerText = text; }
function setActive(id, on) { const el = $(id); if (el) el.classList.toggle('active', !!on); }

// ----------------------------------------------------------------------------
// Envío de parámetros a C++ (POST /api/param)
//   - Si el servidor devuelve `params` (p. ej. al cambiar synthPresetId) se
//     sincroniza la UI con ellos.
// ----------------------------------------------------------------------------
function sendParam(id, value) {
  const numVal = Number(value);
  if (Number.isNaN(numVal)) return Promise.resolve();
  return fetch('/api/param', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: String(id), value: numVal })
  })
  .then(r => r.json())
  .then(res => {
    if (res && res.params && typeof syncFromServerParams === 'function') syncFromServerParams(res.params);
    return res;
  })
  .catch(err => console.error('[sendParam]', id, err));
}

// ----------------------------------------------------------------------------
// Navegación entre páginas (cada página es un HTML independiente)
// ----------------------------------------------------------------------------
const PAGE_URLS = { main: '/', synth: '/synth', organ: '/organ' };
function goToPage(page) { window.location.href = PAGE_URLS[page] || '/'; }

function openSettingsWindow() {
  window.open('/settings', 'NordAudioMidiSetup', 'width=780,height=660');
}

// ----------------------------------------------------------------------------
// Modales
// ----------------------------------------------------------------------------
function openModal(id) {
  if (id === 'modalRev') id = 'modalFxRack';          // alias antiguo
  const el = $(id);
  if (!el) return;
  el.classList.add('open');
  if (id === 'modalEq' && typeof drawEqCurve === 'function') drawEqCurve();
  if (id === 'modalFxSends') {
    if (typeof buildFxSendMatrix === 'function') buildFxSendMatrix();
    if (typeof initAllKnobs === 'function') initAllKnobs();
    if (typeof refreshFxSends === 'function') refreshFxSends();
  }
  if (id === 'modalPianoSample') {
    if (typeof initAllKnobs==='function') initAllKnobs();
    if (typeof refreshPianoSampleEditor==='function') refreshPianoSampleEditor();
    if (typeof refreshFxSends==='function') refreshFxSends();
  }
  if (id === 'modalOrganEffects' && typeof initAllKnobs==='function') initAllKnobs();
  if (id === 'modalBrowser' && typeof renderCategorySidebar === 'function') {
    renderCategorySidebar();
    renderBrowserList();
  }
}
function closeModal(id) {
  const el = $(id);
  if (el) el.classList.remove('open');
}

// ----------------------------------------------------------------------------
// Teclado visual y voces máximas
// ----------------------------------------------------------------------------
function toggleKeyboardVisibility() {
  state.keyboardVisible = !state.keyboardVisible;
  try { localStorage.setItem('nord_kb_vis', state.keyboardVisible ? '1' : '0'); } catch (_) {}
  const kb = $('keyboardSection');
  if (kb) kb.classList.toggle('hidden-kb', !state.keyboardVisible);
  const btn = $('btnToggleKeyboard');
  if (btn) btn.classList.toggle('active-toggle', state.keyboardVisible);
}

function changeMaxVoices(val) {
  setText('maxVoicesTopLabel', val);
  sendParam('maxVoices', val);
}

// ----------------------------------------------------------------------------
// Interruptores de efectos / módulos (LEDs)
// ----------------------------------------------------------------------------
function toggleEffect(key) {
  state[key] = !state[key];
  updateModLeds();
  sendParam(key, state[key] ? 1 : 0);
}

function updateModLeds() {
  ['pianoEnabled', 'eqEnabled', 'delayEnabled', 'reverbEnabled', 'synthEnabled', 'organEnabled', 'organPercEnabled','organChorusEnabled'].forEach(k => {
    const el = $(`led_${k}`);
    if (el) el.classList.toggle('on', !!state[k]);
    const elPage = $(`ledPage_${k}`);
    if (elPage) elPage.classList.toggle('on', !!state[k]);
  });
  const elRackRev = $('ledRack_reverbEnabled');
  if (elRackRev) elRackRev.classList.toggle('on', !!state.reverbEnabled);
  ['chorusEnabled', 'phaserEnabled', 'tremoloEnabled', 'delayEnabled'].forEach(k => {
    const el = $(`ledModal_${k}`);
    if (el) el.classList.toggle('on', !!state[k]);
  });
  const modLed = $('led_modActive');
  if (modLed) modLed.classList.toggle('on', !!state[`${state.activeModTab}Enabled`]);
  if (typeof updateMainDashboard==='function') updateMainDashboard();
}
