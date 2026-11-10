// Roland-inspired analog controls layered on top of the existing Nord Stage visual language.
// Factory programs change ONLY synthesizer layers; user synth patches are independent from
// main panel's 8 complete-performance programs.
const SYNTH_WAVES = [
  [0, 'SAW'], [1, 'PULSE'], [2, 'TRI'], [3, 'SINE'], [4, 'SUPERSAW'],
  [5, 'NOISE'], [6, 'WT'], [8, 'JUNO DCO'], [9, 'JUPITER DUAL']
];
const SYNTH_FILTER_LABELS = ['LP', 'HP', 'BP'];
const SYNTH_OSC2_WAVES = ['SAW', 'PULSE', 'TRI'];
const SYNTH_LFO_WAVES = ['SINE', 'TRI', 'SAW', 'SQUARE'];
const SYNTH_PRESET_COUNT = 8;

function synthKnob(id, min, max, unit, label, log = false, step = false) {
  return `<div class="knob-cell" data-id="${id}" data-min="${min}" data-max="${max}"
    data-val="${Number(state[id] ?? min)}" data-log="${log ? 1 : 0}" data-unit="${unit}"
    data-label="${label}" data-step="${step ? 1 : 0}" data-small="1"></div>`;
}
function synthKnobs(layer, defs) {
  return `<div class="synth-control-grid">${defs.map(d => synthKnob(`L${layer}_${d[0]}`,...d.slice(1))).join('')}</div>`;
}
function synthSection(title, content, open = false) {
  return `<details class="synth-section" ${open ? 'open' : ''}><summary>${title}</summary>${content}</details>`;
}
function buildSynthPresetButtons() {
  const box = $('synthPresetButtons');
  if (!box) return;
  box.innerHTML = '';
  for (let id = 1; id <= SYNTH_PRESET_COUNT; id++) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'mini-mode-pill' + (id === state.synthPresetId ? ' active' : '');
    b.id = `synthPreBtn_${id}`;
    b.textContent = SYNTH_PRESET_NAMES[id].replace(/^P(\d):/, '$1.');
    b.title = `Cargar sonido de fábrica ${SYNTH_PRESET_NAMES[id]} (solo sintetizador)`;
    b.onclick = () => selectSynthPreset(id);
    box.appendChild(b);
  }
}
function buildSynth3LayersPage() {
  const box = $('synth3LayersContainer');
  if (!box) return;
  box.innerHTML = '';
  for (let i = 1; i <= 3; i++) {
    const waves = SYNTH_WAVES.map(([v, label]) =>
      `<button type="button" class="mini-mode-pill" id="wavePill_${i}_${v}"
        onclick="setLayerWave(${i},${v})">${label}</button>`).join('');
    const filters = SYNTH_FILTER_LABELS.map((v, j) =>
      `<button type="button" class="mini-mode-pill" id="filtTypePill_${i}_${j}"
        onclick="setLayerFilterType(${i},${j})">${v}</button>`).join('');
    const osc2 = SYNTH_OSC2_WAVES.map((v,j) =>
      `<button type="button" class="mini-mode-pill" id="osc2Pill_${i}_${j}"
        onclick="setSynthOsc2Wave(${i},${j})">OSC B ${v}</button>`).join('');
    const card = document.createElement('article');
    card.className = 'fx-module-black synth-layer-card';
    card.innerHTML = `
      <div class="mod-pill-header red-pill">
        <button type="button" class="mod-led-btn" id="led_L${i}"
          onclick="toggleSynthLayer(${i-1})" aria-label="Activar capa ${i}"></button>
        <span>SYNTH LAYER ${i}</span><span>DCO / VCF / VCA</span>
      </div>
      <div class="synth-subtitle">FUENTE DE OSCILADORES</div>
      <div class="mode-pills-row">${waves}</div>
      ${synthKnobs(i, [
        ['vol',0,1.5,'%','LEVEL'], ['pan',-1,1,'Pan','PAN'],
        ['oct',-2,2,'oct','OCTAVE',false,true], ['det',-50,50,'ct','FINE TUNE']
      ])}
      ${synthSection('DCO · ANALOG MIX / PWM',
        synthKnobs(i, [
          ['saw',0,1,'%','SAW LVL'], ['pulse',0,1,'%','PULSE LVL'],
          ['sub',0,1,'%','SUB -1 OCT'], ['noise',0,1,'%','NOISE'],
          ['pw',0.05,0.95,'%','PULSE WIDTH'], ['pwmDepth',0,0.45,'%','PWM DEPTH'],
          ['pwmRate',0.05,12,'Hz','PWM RATE',true], ['drive',0,1,'%','DRIVE']
        ]), true)}
      ${synthSection('OSCILLATOR B · JUPITER',
        `<div class="mode-pills-row">${osc2}</div>` + synthKnobs(i,[
          ['osc2',0,1,'%','OSC B MIX'], ['osc2Det',-50,50,'ct','B DETUNE'],
          ['osc2Semi',-12,12,'st','B SEMI',false,true]
        ]))}
      <div class="synth-subtitle">VCF · FILTER</div>
      <div class="mode-pills-row">${filters}</div>
      ${synthKnobs(i,[
        ['cut',40,20000,'Hz','CUTOFF',true], ['res',0.5,12,'Q','RESONANCE'],
        ['hpf',20,6000,'Hz','HPF',true], ['filtEnv',-1,1,'','ENV AMOUNT']
      ])}
      ${synthSection('VCF · FILTER ENVELOPE', synthKnobs(i,[
        ['fAtk',0.001,4,'s','F-ATTACK',true], ['fDec',0.005,10,'s','F-DECAY',true],
        ['fSus',0,1,'%','F-SUSTAIN'], ['fRel',0.005,10,'s','F-RELEASE',true],
        ['filtKT',0,1,'%','KEY TRACK'], ['velCut',0,1,'%','VEL > VCF']
      ]))}
      <div class="synth-subtitle">VCA · AMPLITUDE ENVELOPE</div>
      ${synthKnobs(i,[
        ['atk',0.001,4,'s','ATTACK',true], ['dec',0.02,6,'s','DECAY',true],
        ['sus',0,1,'%','SUSTAIN'], ['rel',0.02,6,'s','RELEASE',true]
      ])}
      ${synthSection('VOICE · UNISON / WAVETABLE',synthKnobs(i,[
        ['unison',1,8,'int','UNISON',false,true], ['uniDet',0,50,'ct','SPREAD DET'],
        ['uniSprd',0,1,'%','STEREO WIDTH'], ['wtIdx',0,63,'int','WAVETABLE',false,true]
      ]))}`;
    box.appendChild(card);
    setLayerWave(i,state[`L${i}_wave`],true);
    setLayerFilterType(i,state[`L${i}_filtType`],true);
    setSynthOsc2Wave(i,state[`L${i}_osc2Wave`],true);
    updateSynthLayerBadges(i,state[`L${i}_en`]);
  }
}
function setLayerWave(layer, wave, silent = false) {
  state[`L${layer}_wave`] = wave;
  SYNTH_WAVES.forEach(([v]) => setActive(`wavePill_${layer}_${v}`, v === wave));
  if (!silent) sendParam(`L${layer}_wave`,wave);
}
function setLayerFilterType(layer, filter, silent = false) {
  state[`L${layer}_filtType`] = filter;
  SYNTH_FILTER_LABELS.forEach((_,j) => setActive(`filtTypePill_${layer}_${j}`,j === filter));
  if (!silent) sendParam(`L${layer}_filtType`,filter);
}
function setSynthOsc2Wave(layer, wave, silent = false) {
  state[`L${layer}_osc2Wave`] = wave;
  SYNTH_OSC2_WAVES.forEach((_,j) => setActive(`osc2Pill_${layer}_${j}`,j === wave));
  if (!silent) sendParam(`L${layer}_osc2Wave`,wave);
}
function toggleSynthLayer(zeroIdx) {
  const i = zeroIdx+1;
  state[`L${i}_en`] = !state[`L${i}_en`];
  updateSynthLayerBadges(i,state[`L${i}_en`]);
  sendParam(`L${i}_en`,state[`L${i}_en`] ? 1 : 0);
}
function updateSynthLayerBadges(i, enabled) {
  const led = $(`led_L${i}`);
  if (led) led.classList.toggle('on',!!enabled);
  const pill = $(`mainPill_L${i}`);
  if (pill) {
    pill.classList.toggle('active',!!enabled);
    pill.textContent = `L${i} ${enabled ? 'ON' : 'OFF'}`;
  }
}
function stepSynthPreset(dir) {
  const current = state.synthPresetId || 1;
  selectSynthPreset(((current - 1 + dir + SYNTH_PRESET_COUNT) % SYNTH_PRESET_COUNT) + 1);
}
function showSynthPreset(id) {
  state.synthPresetId = id;
  setText('synthPresetNameMain',SYNTH_PRESET_NAMES[id] || `Preset ${id}`);
  for (let i=1;i<=SYNTH_PRESET_COUNT;i++) setActive(`synthPreBtn_${i}`,i===id);
}
async function selectSynthPreset(id) {
  showSynthPreset(id);
  if (!state.synthEnabled) {
    state.synthEnabled = true;
    await sendParam('synthEnabled',1);
    updateModLeds();
  }
  await sendParam('synthPresetId',id);
  setSynthPresetStatus(`Fábrica: ${SYNTH_PRESET_NAMES[id]}`);
}
function setSynthLfoEnabled(silent = false) {
  state.synthLfoEnabled = silent ? !!state.synthLfoEnabled : !state.synthLfoEnabled;
  setActive('synthLfoEnabledBtn',state.synthLfoEnabled);
  setText('synthLfoEnabledBtn',state.synthLfoEnabled ? 'LFO ON' : 'LFO OFF');
  if (!silent) sendParam('synthLfoEnabled',state.synthLfoEnabled ? 1 : 0);
}
function setSynthLfoWave(wave,silent=false) {
  state.synthLfoWave=wave;
  SYNTH_LFO_WAVES.forEach((_,i)=>setActive(`synthLfoWave_${i}`,i===wave));
  if(!silent) sendParam('synthLfoWave',wave);
}
function buildSynthGlobalControls() {
  const box=$('synthGlobalControls');
  if(!box) return;
  box.innerHTML=`
    <div class="synth-global-heading"><b>MASTER / MODULATION</b>
      <span>Controles del sintetizador · 3 capas</span></div>
    <div class="synth-global-controls">
      ${synthKnob('synthMasterVol',0,1.5,'%','MASTER')}
      ${synthKnob('synthMacroCutoff',0.1,2,'','VCF MACRO')}
      ${synthKnob('synthLfoRate',0.05,30,'Hz','LFO RATE',true)}
      ${synthKnob('synthLfoDepth',0,1,'%','LFO DEPTH')}
      ${synthKnob('synthLfoPitch',0,200,'ct','LFO PITCH',false,true)}
      ${synthKnob('synthLfoCutoff',-5000,5000,'Hz','LFO VCF')}
      <div class="synth-global-lfo"><button type="button" id="synthLfoEnabledBtn"
        class="mini-mode-pill" onclick="setSynthLfoEnabled()">LFO OFF</button>
        <div class="mode-pills-row">${SYNTH_LFO_WAVES.map((w,i)=>
          `<button type="button" class="mini-mode-pill" id="synthLfoWave_${i}"
          onclick="setSynthLfoWave(${i})">${w}</button>`).join('')}</div>
      </div>
    </div>`;
  setSynthLfoEnabled(true);
  setSynthLfoWave(state.synthLfoWave,true);
}

function setSynthPresetStatus(message,isError=false) {
  const el=$('synthPatchStatus');
  if(el){ el.textContent=message; el.classList.toggle('error',isError); }
}
async function synthPresetRequest(path,data) {
  const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  if(!r.ok) throw new Error(`HTTP ${r.status}`);
  const json=await r.json();
  if(!json.ok) throw new Error(json.error||'Operación no completada');
  return json;
}
async function refreshSynthUserPresets(selected) {
  const box=$('synthUserPresetSelect');
  if(!box) return;
  try{
    const r=await fetch('/api/synth/presets');
    if(!r.ok) throw new Error(`HTTP ${r.status}`);
    const patches=await r.json();
    box.replaceChildren(new Option('— MIS PRESETS —',''));
    (Array.isArray(patches)?patches:[]).forEach(p => box.add(new Option(p.name,p.name)));
    if(selected) box.value=selected;
  }catch(e){setSynthPresetStatus(`Error de biblioteca: ${e.message}`,true);}
}
async function saveSynthUserPreset(){
  const box=$('synthUserPresetSelect');
  const suggested=box?.value||'';
  const name=window.prompt('Nombre del preset de sintetizador (solo 3 capas y LFO):',suggested);
  if(!name || !name.trim()) return;
  try{
    await synthPresetRequest('/api/synth/presets/save',{name:name.trim()});
    await refreshSynthUserPresets(name.trim());
    setSynthPresetStatus(`Preset de sintetizador guardado: ${name.trim()}`);
  }catch(e){setSynthPresetStatus(e.message,true);}
}
async function loadSynthUserPreset(){
  const name=$('synthUserPresetSelect')?.value;
  if(!name) return;
  try{
    const out=await synthPresetRequest('/api/synth/presets/load',{name});
    if(out.params) syncFromServerParams(out.params);
    setSynthPresetStatus(`Preset de sintetizador cargado: ${name}`);
  }catch(e){setSynthPresetStatus(e.message,true);}
}
async function deleteSynthUserPreset(){
  const name=$('synthUserPresetSelect')?.value;
  if(!name||!window.confirm(`¿Eliminar el preset «${name}»?`)) return;
  try{
    await synthPresetRequest('/api/synth/presets/delete',{name});
    await refreshSynthUserPresets();
    setSynthPresetStatus(`Preset eliminado: ${name}`);
  }catch(e){setSynthPresetStatus(e.message,true);}
}
