// ============================================================================
// ui/js/organ.js — Órgano: drawbars, modelo, vibrato/chorus, percusión y rotary
// Modelos C++: 0=B3 Tonewheel · 1=Vox Continental · 2=Farfisa · 3=Pipe Organ
// Vibrato:     0=Off · 1-3=V1-V3 · 4-6=C1-C3
// ============================================================================

const DRAWBAR_NAMES = ["16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'"];
// Hammond-style color families: brown (pedal/sub), ivory and black harmonic tabs.
const DRAWBAR_TONES = ['brown','brown','ivory','ivory','black','ivory','black','black','ivory'];
const ORGAN_MODELS  = ['B3 TONEWHEEL', 'VOX CONTINENTAL', 'FARFISA', 'PIPE ORGAN'];
const ORGAN_VIBRATO = ['OFF', 'V1', 'V2', 'V3', 'C1', 'C2', 'C3'];

function buildOrganDrawbarsPage() {
  const deck=$('organDrawbarsDeck');
  if(!deck)return;
  deck.innerHTML='';
  for(let i=0;i<9;i++){
    const col=document.createElement('div');
    col.className='drawbar-column';
    col.dataset.tone=DRAWBAR_TONES[i];
    col.innerHTML=`
      <div class="drawbar-label">${DRAWBAR_NAMES[i]}</div>
      <div class="drawbar-track-wrap">
        <div class="drawbar-guide"></div>
        <div class="drawbar-scale">${Array.from({length:9},(_,n)=>`<span>${n}</span>`).join('')}</div>
        <div class="drawbar-handle" id="dbHandle_${i}" aria-hidden="true"></div>
        <input type="range" class="drawbar-input-range" id="dbRange_${i}"
          aria-label="Drawbar ${DRAWBAR_NAMES[i]}" min="0" max="8" step="1"
          value="${Math.round((state['drawbar'+i]||0)*8)}" oninput="onDrawbarSlider(${i},this.value)">
      </div>
      <output class="drawbar-val-badge" id="dbValBadge_${i}">0</output>`;
    deck.appendChild(col);
    updateDrawbarVisual(i,state['drawbar'+i]||0);
  }
}

function buildOrganOptions() {
  const models = $('organModelPills');
  if (models) {
    models.innerHTML = ORGAN_MODELS.map((n, i) =>
      `<span class="mini-mode-pill" id="organModel_${i}" onclick="setOrganModel(${i})">${n}</span>`).join('');
  }
  const vib = $('organVibratoPills');
  if (vib) {
    vib.innerHTML = ORGAN_VIBRATO.map((n, i) =>
      `<span class="mini-mode-pill" id="organVib_${i}" onclick="setOrganVibrato(${i})">${n}</span>`).join('');
  }
  updateOrganControlsVisual();
}

function onDrawbarSlider(idx, valStr) {
  // Hardware pull steps 0..8; DSP still receives normalized 0..1.
  const step=Math.min(8,Math.max(0,parseInt(valStr,10)||0));
  const val=step/8;
  state['drawbar'+idx]=val;
  updateDrawbarVisual(idx,val);
  sendParam('drawbar'+idx,val);
}

function updateDrawbarVisual(idx, normVal) {
  const v=Math.min(1,Math.max(0,Number(normVal)||0));
  const step=Math.round(v*8);
  const handle=$('dbHandle_'+idx);
  if(handle)handle.style.top=(16+step/8*68)+'%';
  setText('dbValBadge_'+idx,step);
  const slider=$('dbRange_'+idx);
  if(slider && document.activeElement!==slider)slider.value=String(step);
}

function setOrganModel(m)   { state.organModel = m;        updateOrganControlsVisual(); sendParam('organModel', m); }
function setOrganVibrato(v) { state.organVibratoMode = v;  updateOrganControlsVisual(); sendParam('organVibratoMode', v); }
function toggleOrganPerc() {
  state.organPercEnabled = !state.organPercEnabled;
  updateOrganControlsVisual();
  sendParam('organPercEnabled', state.organPercEnabled ? 1 : 0);
}
function toggleOrganPercHarmonic() {
  state.organPercHarmonic = (state.organPercHarmonic === 3) ? 2 : 3;
  updateOrganControlsVisual();
  sendParam('organPercHarmonic', state.organPercHarmonic);
}
function toggleOrganRotary() {
  state.organRotaryFast = !state.organRotaryFast;
  updateOrganRotaryVisual();
  sendParam('organRotaryFast', state.organRotaryFast ? 1 : 0);
}

function updateOrganControlsVisual() {
  ORGAN_MODELS.forEach((_, i)  => setActive(`organModel_${i}`, i === state.organModel));
  ORGAN_VIBRATO.forEach((_, i) => setActive(`organVib_${i}`,  i === state.organVibratoMode));
  setActive('organPercToggle', state.organPercEnabled);
  setText('organPercToggle', state.organPercEnabled ? 'PERC: ON' : 'PERC: OFF');
  setText('organPercHarm', state.organPercHarmonic === 3 ? '3ª ARMÓNICA (2⅔′)' : '2ª ARMÓNICA (4′)');
  updateOrganRotaryVisual();
}

function updateOrganRotaryVisual() {
  const pMain = $('pillRotaryMain');
  if (pMain) {
    pMain.innerText = state.organRotaryFast ? 'ROTARY: FAST' : 'ROTARY: SLOW';
    pMain.classList.toggle('active', state.organRotaryFast);
  }
  const bPage = $('btnRotaryPage');
  if (bPage) bPage.innerText = state.organRotaryFast ? 'ROTARY SPEAKER: FAST (TREMOLO)' : 'ROTARY SPEAKER: SLOW (CHORALE)';
}

// Bank for organ-only presets. The eight hardware program pads remain snapshots
// of the entire workstation and do not share this bank.
function setOrganPresetStatus(message,isError=false) {
  const el=$('organPatchStatus');
  if(el){el.textContent=message;el.classList.toggle('error',isError);}
}
async function organPresetRequest(path,data){
  const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  if(!r.ok) throw new Error(`HTTP ${r.status}`);
  const out=await r.json();
  if(!out.ok) throw new Error(out.error||'Operación no completada');
  return out;
}
async function refreshOrganUserPresets(selected){
  const box=$('organUserPresetSelect');
  if(!box)return;
  try{
    const r=await fetch('/api/organ/presets');
    if(!r.ok)throw new Error(`HTTP ${r.status}`);
    const presets=await r.json();
    box.replaceChildren(new Option('— MIS PRESETS —',''));
    (Array.isArray(presets)?presets:[]).forEach(p=>box.add(new Option(p.name,p.name)));
    if(selected) box.value=selected;
  }catch(e){setOrganPresetStatus(e.message,true);}
}
async function saveOrganUserPreset(){
  const suggested=$('organUserPresetSelect')?.value||'';
  const name=window.prompt('Nombre del preset de órgano:',suggested);
  if(!name||!name.trim())return;
  try{
    await organPresetRequest('/api/organ/presets/save',{name:name.trim()});
    await refreshOrganUserPresets(name.trim());
    setOrganPresetStatus(`Órgano guardado: ${name.trim()}`);
  }catch(e){setOrganPresetStatus(e.message,true);}
}
async function loadOrganUserPreset(){
  const name=$('organUserPresetSelect')?.value;
  if(!name)return;
  try{
    const out=await organPresetRequest('/api/organ/presets/load',{name});
    if(out.params) syncFromServerParams(out.params);
    setOrganPresetStatus(`Órgano cargado: ${name}`);
  }catch(e){setOrganPresetStatus(e.message,true);}
}
async function deleteOrganUserPreset(){
  const name=$('organUserPresetSelect')?.value;
  if(!name||!window.confirm(`¿Eliminar el preset «${name}»?`))return;
  try{
    await organPresetRequest('/api/organ/presets/delete',{name});
    await refreshOrganUserPresets();
    setOrganPresetStatus(`Órgano eliminado: ${name}`);
  }catch(e){setOrganPresetStatus(e.message,true);}
}
