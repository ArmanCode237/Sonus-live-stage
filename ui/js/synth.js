// Roland-inspired analog controls layered on top of the existing Nord Stage visual language.
// Factory programs change ONLY synthesizer layers; user synth patches are independent from
// main panel's 8 complete-performance programs.
const SYNTH_WAVES = [
  [0, 'SAW'], [1, 'PULSE'], [2, 'TRI'], [3, 'SINE'], [4, 'SUPERSAW'],
  [5, 'NOISE'], [6, 'WT'], [7, 'HARD SYNC'], [8, 'JUNO DCO'], [9, 'JUPITER DUAL'],
  [10, '2-OP FM'], [11, 'SUPERPULSE'], [12, 'RING MOD'], [13, 'VOX CHOIR'], [14, 'WAVEFOLDER']
];
const SYNTH_FILTER_LABELS = ['LP', 'HP', 'BP', 'NOTCH'];
const SYNTH_OSC2_WAVES = ['SAW', 'PULSE', 'TRI'];
const SYNTH_LFO_WAVES = ['SINE', 'TRI', 'SAW', 'SQUARE'];
const SYNTH_PRESET_COUNT = 120;

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
  const box=$('synthPresetButtons'); if(!box)return;
  // Native selector is a second, accessible selection path. It also remains
  // usable if the visual factory cards are obstructed by CSS/layout.
  const direct=$('synthFactoryDirectSelect');
  if(direct && direct.options.length<=1) {
    for(let id=1;id<=SYNTH_PRESET_COUNT;id++)
      direct.add(new Option(`${String(id).padStart(3,'0')} — ${SYNTH_PRESET_NAMES[id] || `Preset ${id}`}`,String(id)));
    direct.onchange=()=>{ if(direct.value) selectSynthPreset(Number(direct.value)); };
  }
  const categories=['ALL','BRASS','LEADS','KEYS','POLYKEYS','PADS','STRINGS','BASS','BELLS','SAWS'];
  const pills=$('synthCategoryPills');
  if(pills && !pills.children.length){
    categories.forEach(category=>{
      const button=document.createElement('button');button.type='button';
      button.className='synth-category-pill';button.dataset.family=category;
      button.textContent=category==='ALL'?'ALL PATCHES':category;
      button.onclick=()=>{const s=$('synthFactoryCategory');if(s)s.value=category;buildSynthPresetButtons();};
      pills.appendChild(button);
    });
  }
  const cat=$('synthFactoryCategory')?.value || 'ALL';
  if(pills)Array.from(pills.children).forEach(b=>{const active=b.dataset.family===cat;b.classList.toggle('active',active);b.setAttribute('aria-pressed',String(active));});
  const term=($('synthFactorySearch')?.value || '').trim().toLocaleLowerCase();
  box.replaceChildren();
  let matches=0;
  for(let id=1;id<=SYNTH_PRESET_COUNT;id++){
    const name=SYNTH_PRESET_NAMES[id] || `Preset ${id}`;
    const family=SYNTH_PRESET_CATEGORIES[id] || 'OTHER';
    if(cat!=='ALL'&&cat!==family)continue;
    if(term && !(`${name} ${family}`.toLowerCase().includes(term)))continue;
    const b=document.createElement('button'); b.type='button';
    b.className='mini-mode-pill factory-program-tile'+(id===state.synthPresetId?' active':'');
    b.dataset.family=family;
    b.id=`synthPreBtn_${id}`;b.title=`${family} · ${name}`;
    const num=document.createElement('span');num.className='factory-num';num.textContent=String(id).padStart(3,'0');
    const label=document.createElement('span');label.className='factory-label';label.textContent=name;
    const badge=document.createElement('small');badge.className='factory-category';badge.textContent=family;
    b.append(num,label,badge);b.onclick=()=>selectSynthPreset(id);
    box.appendChild(b);matches++;
  }
  const output=$('synthFactoryResultCount');if(output)output.textContent=`${matches} / 120 PATCHES`;
}
// Paneles visuales por capa: OSC / FILTER / ENVELOPES / VOICE.
// No se modifica ningún identificador de parámetro (L1_..., L2_..., L3_...).
const SYNTH_UI_PANELS = ['osc','filter','env','voice'];
const synthUiSelectedPanels = {1:'osc',2:'osc',3:'osc'};
function setSynthActivePanel(layer,panel) {
  if(!SYNTH_UI_PANELS.includes(panel)) return;
  synthUiSelectedPanels[layer]=panel;
  SYNTH_UI_PANELS.forEach(p => {
    const button=$(`synthTab_${layer}_${p}`), content=$(`synthPanel_${layer}_${p}`);
    if(button){
      button.classList.toggle('active',p===panel);
      button.setAttribute('aria-selected',String(p===panel));
      button.tabIndex=p===panel ? 0 : -1;
    }
    if(content) content.hidden=(p!==panel);
  });
}
function updateSynthWavePreview(layer,wave) {
  const wavePaths={
    0:'M 4 48 L 38 16 L 38 48 L 72 16 L 72 48 L 106 16 L 106 48 L 140 16',
    1:'M 4 48 L 4 18 L 38 18 L 38 48 L 72 48 L 72 18 L 106 18 L 106 48 L 140 48',
    2:'M 4 48 L 38 16 L 72 48 L 106 16 L 140 48',
    3:'M 4 32 C 20 6,39 6,56 32 S 92 58,108 32 S 126 6,140 32',
    4:'M 4 48 L 21 16 L 21 48 L 38 16 L 38 48 L 55 16 L 55 48 L 72 16 L 72 48 L 89 16 L 89 48 L 106 16 L 106 48 L 123 16 L 123 48 L 140 16',
    5:'M 4 30 L 12 16 L 19 42 L 26 24 L 35 53 L 43 22 L 54 35 L 62 13 L 74 46 L 82 25 L 94 51 L 108 20 L 117 33 L 127 18 L 140 47',
    6:'M 4 38 L 12 23 L 22 45 L 32 17 L 42 32 L 54 21 L 65 45 L 74 20 L 84 30 L 92 15 L 104 39 L 113 21 L 123 45 L 140 27',
    7:'M 4 48 L 72 16 L 4 48 L 72 16 L 140 48',
    8:'M 4 48 L 40 16 L 40 48 L 74 16 L 74 48 L 108 16 L 108 48 L 140 16',
    9:'M 4 45 L 30 18 L 30 48 L 48 24 L 66 48 L 91 16 L 91 48 L 116 20 L 140 45',
    10:'M 4 32 C 15 10,25 50,35 32 S 55 10,65 32 S 85 50,95 32 S 115 10,140 32',
    11:'M 4 48 L 4 15 L 25 15 L 25 48 L 50 48 L 50 15 L 75 15 L 75 48 L 100 48 L 100 15 L 140 48',
    12:'M 4 32 L 35 8 L 72 56 L 105 12 L 140 32',
    13:'M 4 35 C 18 15,36 15,50 35 S 85 55,100 35 S 122 15,140 35',
    14:'M 4 24 L 38 52 L 72 12 L 106 52 L 140 24'
  };
  const path=$(`synthWavePath_${layer}`);
  if(path)path.setAttribute('d',wavePaths[wave]||wavePaths[0]);
  const label=$(`synthWaveLabel_${layer}`);
  if(label)label.textContent=(SYNTH_WAVES.find(([v])=>v===wave)||[0,'SAW'])[1];
}
function buildSynth3LayersPage() {
  const box=$('synth3LayersContainer');
  if(!box)return;
  box.innerHTML='';
  for(let i=1;i<=3;i++){
    const waveButtons=SYNTH_WAVES.map(([v,label])=>
      `<button type="button" class="mini-mode-pill synth-select-wave" id="wavePill_${i}_${v}"
         onclick="setLayerWave(${i},${v})">${label}</button>`).join('');
    const filterButtons=SYNTH_FILTER_LABELS.map((label,j)=>
      `<button type="button" class="mini-mode-pill" id="filtTypePill_${i}_${j}"
         onclick="setLayerFilterType(${i},${j})">${label}</button>`).join('');
    const osc2Buttons=SYNTH_OSC2_WAVES.map((label,j)=>
      `<button type="button" class="mini-mode-pill" id="osc2Pill_${i}_${j}"
         onclick="setSynthOsc2Wave(${i},${j})">${label}</button>`).join('');
    const tabs=[['osc','DCO'],['filter','VCF'],['env','ENVELOPES'],['voice','VOICE']]
      .map(([k,label])=>`<button type="button" role="tab" id="synthTab_${i}_${k}" class="synth-panel-tab"
        aria-controls="synthPanel_${i}_${k}" aria-selected="false" onclick="setSynthActivePanel(${i},'${k}')">${label}</button>`).join('');
    const card=document.createElement('article');
    card.className='synth-layer-card';
    card.setAttribute('aria-label',`Sintetizador capa ${i}`);
    card.innerHTML=`
      <header class="synth-layer-topbar">
        <div class="synth-layer-ident"><span class="synth-layer-index">${String(i).padStart(2,'0')}</span>
          <div><b>LAYER ${String.fromCharCode(64+i)}</b><span>INDEPENDENT ANALOG VOICE</span></div></div>
        <button type="button" class="synth-layer-power" onclick="toggleSynthLayer(${i-1})"
          aria-label="Encender o apagar capa ${i}"><span class="mod-led-btn" id="led_L${i}"></span> <span id="synthLayerPowerText_${i}">ON</span></button>
      </header>
      <div class="synth-layer-wavefront">
        <div class="synth-wave-display" aria-hidden="true">
          <div class="synth-wave-display-top"><span>DCO A</span><b id="synthWaveLabel_${i}">SAW</b></div>
          <svg viewBox="0 0 144 64" preserveAspectRatio="none"><path class="synth-wave-grid" d="M0 16 H144 M0 32 H144 M0 48 H144 M24 0 V64 M48 0 V64 M72 0 V64 M96 0 V64 M120 0 V64"/>
            <path class="synth-wave-path" id="synthWavePath_${i}" d="M4 48 L38 16 L38 48 L72 16 L72 48 L106 16 L106 48 L140 16"/></svg>
        </div>
        <div class="synth-layer-pitch"><span class="synth-subtitle">PERFORMANCE</span>
          ${synthKnobs(i,[['vol',0,1.5,'%','LEVEL'],['pan',-1,1,'Pan','PAN'],['oct',-2,2,'oct','OCTAVE',false,true],['det',-50,50,'ct','FINE']])}
        </div>
      </div>
      <div class="synth-layer-tabs" role="tablist" aria-label="Secciones de síntesis capa ${i}">${tabs}</div>
      <section class="synth-tab-panel" id="synthPanel_${i}_osc" role="tabpanel">
        <div class="synth-subtitle synth-subtitle-number"><span>01 / OSCILLATORS</span><span>ANALOG / MULTI-WAVE</span></div>
        <div class="mode-pills-row synth-choice-row">${waveButtons}</div>
        ${synthKnobs(i,[['saw',0,1,'%','SAW'],['pulse',0,1,'%','PULSE'],['sub',0,1,'%','SUB OSC'],['noise',0,1,'%','NOISE'],['pw',0.05,0.95,'%','PULSE WIDTH'],['pwmDepth',0,0.45,'%','PWM DEPTH'],['pwmRate',0.05,12,'Hz','PWM RATE',true],['drive',0,1,'%','DRIVE']])}
        <details class="synth-sub-drawer" open><summary>DCO B <span>DUAL OSCILLATOR</span></summary>
          <div class="mode-pills-row synth-choice-row">${osc2Buttons}</div>
          ${synthKnobs(i,[['osc2',0,1,'%','OSC B MIX'],['osc2Det',-50,50,'ct','DETUNE'],['osc2Semi',-12,12,'st','SEMITONES',false,true]])}
        </details>
      </section>
      <section class="synth-tab-panel" id="synthPanel_${i}_filter" role="tabpanel" hidden>
        <div class="synth-subtitle synth-subtitle-number"><span>02 / FILTER</span><span>VCF + HPF</span></div>
        <div class="mode-pills-row synth-choice-row">${filterButtons}</div>
        ${synthKnobs(i,[['cut',40,20000,'Hz','CUTOFF',true],['res',0.5,12,'Q','RESONANCE'],['hpf',20,6000,'Hz','HIGH PASS',true],['filtEnv',-1,1,'','ENV AMOUNT'],['filtKT',0,1,'%','KEY TRACK'],['velCut',0,1,'%','VEL > VCF']])}
        <div class="synth-tip">Filtro por voz · seguimiento del teclado y velocidad</div>
      </section>
      <section class="synth-tab-panel" id="synthPanel_${i}_env" role="tabpanel" hidden>
        <div class="synth-subtitle synth-subtitle-number"><span>03 / ENVELOPES</span><span>VCA / VCF</span></div>
        <div class="synth-env-caption">AMPLITUDE / VCA</div>
        ${synthKnobs(i,[['atk',0.001,4,'s','ATTACK',true],['dec',0.02,6,'s','DECAY',true],['sus',0,1,'%','SUSTAIN'],['rel',0.02,6,'s','RELEASE',true]])}
        <div class="synth-env-caption">FILTER / VCF</div>
        ${synthKnobs(i,[['fAtk',0.001,4,'s','F-ATTACK',true],['fDec',0.005,10,'s','F-DECAY',true],['fSus',0,1,'%','F-SUSTAIN'],['fRel',0.005,10,'s','F-RELEASE',true]])}
      </section>
      <section class="synth-tab-panel" id="synthPanel_${i}_voice" role="tabpanel" hidden>
        <div class="synth-subtitle synth-subtitle-number"><span>04 / VOICE</span><span>UNISON / WAVETABLE</span></div>
        ${synthKnobs(i,[['unison',1,8,'int','UNISON',false,true],['uniDet',0,50,'ct','SPREAD DET'],['uniSprd',0,1,'%','STEREO'],['wtIdx',0,63,'int','WAVETABLE',false,true]])}
        <div class="synth-tip">Los controles de voz afectan solamente esta capa.</div>
      </section>
      <footer class="synth-layer-footer"><span class="pilot-led"></span> <span>ENGINE ${String(i).padStart(2,'0')}</span><span>INDEPENDENT CONTROL</span></footer>`;
    box.appendChild(card);
    setSynthActivePanel(i,synthUiSelectedPanels[i]||'osc');
    setLayerWave(i,state[`L${i}_wave`],true);
    setLayerFilterType(i,state[`L${i}_filtType`],true);
    setSynthOsc2Wave(i,state[`L${i}_osc2Wave`],true);
    updateSynthLayerBadges(i,state[`L${i}_en`]);
  }
}
function setLayerWave(layer, wave, silent = false) {
  state[`L${layer}_wave`] = wave;
  SYNTH_WAVES.forEach(([v]) => setActive(`wavePill_${layer}_${v}`, v === wave));
  updateSynthWavePreview(layer,wave);
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
  const card=led?.closest('.synth-layer-card');
  if (card) card.classList.toggle('is-muted',!enabled);
  setText(`synthLayerPowerText_${i}`,enabled?'ON':'OFF');
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
// A factory patch is not considered loaded until the C++ engine confirms its ID.
// Rebuilding the preset DOM in every /api/state sync destroys a button between
// pointerdown and pointerup; with MIDI/polling activity the click may never fire.
let synthFactoryRequestSequence = 0;
let synthFactoryRequestQueue = Promise.resolve();
let synthFactoryPendingId = null;
let synthFactoryStatusLast = null;
const SYNTH_FACTORY_API_EXPECTED = 3;

function setSynthFactoryEngineStatus(message,isError=false) {
  const el=$('synthFactoryEngineStatus');
  if(el){ el.textContent=message; el.classList.toggle('error',isError); }
}
async function fetchSynthFactoryStatus() {
  const r=await fetch('/api/synth/factory/status',{cache:'no-store',headers:{'Cache-Control':'no-cache'}});
  if(r.status===404) throw new Error('MOTOR_ANTIGUO: el ejecutable en 8080 no ofrece la API actual');
  if(!r.ok) throw new Error(`HTTP ${r.status} al leer el estado del motor`);
  const obj=await r.json();
  if(!obj.ok || obj.apiVersion!==SYNTH_FACTORY_API_EXPECTED || obj.maxFactoryPresets!==120)
    throw new Error(`Versión incompatible: ${obj.build || 'desconocida'} (API ${obj.apiVersion || '?'})`);
  synthFactoryStatusLast=obj;
  setSynthFactoryEngineStatus(`${obj.build} · Preset ${String(obj.presetId).padStart(3,'0')} · ${obj.enabledLayers} capas · Motor activo: ${obj.synthEnabled?'SÍ':'NO'}`);
  return obj;
}
async function verifySynthFactoryStatus(manual=false) {
  try {
    const status=await fetchSynthFactoryStatus();
    if(synthFactoryPendingId===null) showSynthPreset(status.presetId);
    if(manual) setSynthPresetStatus(`Motor verificado: preset ${String(status.presetId).padStart(3,'0')} · ${status.build}`);
    return status;
  } catch(error) {
    setSynthFactoryEngineStatus(error.message,true);
    if(manual) setSynthPresetStatus(`Conexión de presets: ${error.message}`,true);
    return null;
  }
}

function showSynthPreset(id) {
  const actual = Number(id);
  if (!Number.isInteger(actual) || actual < 1 || actual > SYNTH_PRESET_COUNT) return;
  // Ignore stale polling responses while a patch change is in flight.
  if (synthFactoryPendingId !== null && actual !== synthFactoryPendingId) return;
  state.synthPresetId = actual;
  const direct=$('synthFactoryDirectSelect');
  if(direct) direct.value=String(actual);
  const name = SYNTH_PRESET_NAMES[actual] || `Preset ${actual}`;
  setText('synthPresetNameMain',name);
  setText('synthScreenPatch',name);
  setText('synthScreenNumber',String(actual).padStart(3,'0'));
  setText('synthFeaturedName',name);
  setText('synthFeaturedNumber',String(actual).padStart(3,'0')+' / 120');
  setText('synthFeaturedCategory',SYNTH_PRESET_CATEGORIES[actual] || 'ANALOG');
  // IMPORTANT: only change CSS classes, never replace the buttons here.
  for (let i = 1; i <= SYNTH_PRESET_COUNT; i++) setActive(`synthPreBtn_${i}`,i===actual);
}

async function postSynthFactoryParam(id, value) {
  const response = await fetch('/api/param', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id,value})
  });
  if (!response.ok) throw new Error(`HTTP ${response.status} al modificar ${id}`);
  const result = await response.json();
  if (result.ok !== true) throw new Error(`Parámetro no reconocido por el motor: ${id}`);
  return result;
}

async function requestSynthFactoryPreset(id) {
  // New atomic API: power ON + load the factory patch in a single transaction.
  // Older builds can still use the existing /api/param route as a fallback.
  const response = await fetch('/api/synth/factory/load', {
    method:'POST', cache:'no-store', headers:{'Content-Type':'application/json'},
    body:JSON.stringify({id})
  });
  if (response.status === 404) {
    await postSynthFactoryParam('synthEnabled',1);
    return postSynthFactoryParam('synthPresetId',id);
  }
  if (!response.ok) {
    let message = `HTTP ${response.status}`;
    try { const error = await response.json(); message = error.error || message; } catch (_) {}
    throw new Error(message);
  }
  const result = await response.json();
  if (result.ok !== true) throw new Error(result.error || 'No se pudo cargar el sonido');
  return result;
}

function selectSynthPreset(id) {
  const requested = Number(id);
  if (!Number.isInteger(requested) || requested < 1 || requested > SYNTH_PRESET_COUNT) {
    setSynthPresetStatus('Preset no válido',true);
    return Promise.resolve(false);
  }
  const serial = ++synthFactoryRequestSequence;
  synthFactoryPendingId = requested;
  setSynthPresetStatus(`Cargando ${String(requested).padStart(3,'0')} · ${SYNTH_PRESET_NAMES[requested]}…`);
  // Serialize requests: clicking two patches quickly must leave the LAST one active.
  synthFactoryRequestQueue = synthFactoryRequestQueue.catch(() => {}).then(async () => {
    if (serial !== synthFactoryRequestSequence) return false;
    try {
      const result = await requestSynthFactoryPreset(requested);

      // Successful POST is verified against the active engine below.
      const confirmed = Number(result.params?.synthPresetId ?? result.presetId);
      if (confirmed !== requested) {
        // Reflect the sound that the engine actually kept, not a false choice.
        if (serial === synthFactoryRequestSequence) {
          synthFactoryPendingId = null;
          if (result.params && typeof syncFromServerParams === 'function') syncFromServerParams(result.params);
          if (Number.isInteger(confirmed)) showSynthPreset(confirmed);
        }
        throw new Error(`El motor devolvió el preset ${Number.isFinite(confirmed) ? confirmed : 'desconocido'} en lugar del ${requested}. Comprueba que recompilaste el C++ de V8.`);
      }
      // An HTTP 200 is NOT proof that the audio engine actually retained the patch.
      // Confirm the live parameter state through an independent request.
      let observed=null;
      try {
        observed=await fetchSynthFactoryStatus();
      } catch (verificationError) {
        setSynthFactoryEngineStatus(verificationError.message,true);
        // Older builds still support POST /api/param; display a warning instead
        // of manufacturing an independent audio-engine confirmation.
        if(!String(verificationError.message).startsWith('MOTOR_ANTIGUO:')) throw verificationError;
      }
      if(observed && (observed.presetId!==requested || !observed.synthEnabled))
        throw new Error(`El motor informa preset ${observed.presetId} (ON=${observed.synthEnabled}) en lugar de ${requested}.`);
      if (serial !== synthFactoryRequestSequence) return false;
      synthFactoryPendingId = null;
      state.synthEnabled = true;
      // Visual sync must not turn a successful audio-engine load into an error.
      if (result.params && typeof syncFromServerParams === 'function') {
        try { syncFromServerParams(result.params); }
        catch(uiError) { console.error('[synth] UI sync after successful factory load',uiError); }
      }
      showSynthPreset(confirmed);
      if (typeof updateModLeds === 'function') updateModLeds();
      const audible = observed && (observed.synthMasterVol <= 0.001 || observed.enabledLayers === 0);
      if (audible)
        setSynthPresetStatus(`Cargado ${String(confirmed).padStart(3,'0')}, pero no habrá sonido: revisa MASTER y capas activas.`,true);
      else if (!observed)
        setSynthPresetStatus(`Recibido ${String(confirmed).padStart(3,'0')} · ${SYNTH_PRESET_NAMES[confirmed]}. Verificación no disponible: motor antiguo.`,true);
      else
        setSynthPresetStatus(`Motor confirmado: ${String(confirmed).padStart(3,'0')} · ${SYNTH_PRESET_NAMES[confirmed]}`);
      return true;
    } catch (error) {
      if (serial === synthFactoryRequestSequence) {
        synthFactoryPendingId = null;
        setSynthPresetStatus(`No se cargó el preset: ${error.message}`,true);
        console.error('[synth factory]',error);
      }
      return false;
    }
  });
  return synthFactoryRequestQueue;
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
function setSynthVoiceMode(mode,silent=false) {
  mode=Math.max(0,Math.min(2,Number(mode)||0));state.synthVoiceMode=mode;
  for(let i=0;i<3;i++)setActive(`synthVoiceMode_${i}`,i===mode);
  if(!silent)sendParam('synthVoiceMode',mode);
}
function setSynthGlideMode(mode,silent=false) {
  mode=Number(mode)===0?0:1;state.synthGlideMode=mode;
  for(let i=0;i<2;i++)setActive(`synthGlideMode_${i}`,i===mode);
  if(!silent)sendParam('synthGlideMode',mode);
}
function buildSynthGlobalControls() {
  const box=$('synthGlobalControls');
  if(!box) return;
  box.innerHTML=`
    <div class="synth-global-heading"><b><span class="pilot-led"></span> MASTER / MODULATION</b>
      <span>SYNTHESIS BUS · 3 INDEPENDENT LAYERS</span></div>
    <div class="synth-global-controls">
      ${synthKnob('synthMasterVol',0,1.5,'%','MASTER')}
      ${synthKnob('synthMacroCutoff',0.1,2,'','VCF MACRO')}
      ${synthKnob('synthPortamento',0,2,'s','PORTAMENTO')}
      ${synthKnob('synthSlide',-12,12,'st','PITCH SLIDE')}
      <div class="synth-global-lfo synth-performance-modes">
        <span class="synth-subtitle">VOICE / GLIDE</span>
        <div class="mode-pills-row">
          ${['POLY','MONO','LEGATO'].map((w,i)=>`<button type="button" class="mini-mode-pill" id="synthVoiceMode_${i}" onclick="setSynthVoiceMode(${i})">${w}</button>`).join('')}
        </div>
        <div class="mode-pills-row">
          ${['ALWAYS','LEGATO ONLY'].map((w,i)=>`<button type="button" class="mini-mode-pill" id="synthGlideMode_${i}" onclick="setSynthGlideMode(${i})">${w}</button>`).join('')}
        </div>
      </div>
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
  setSynthVoiceMode(state.synthVoiceMode,true);
  setSynthGlideMode(state.synthGlideMode,true);
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
  }catch(e){setSynthPresetStatus(`Error de biblioteca: ${e.message}`,true);_}
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