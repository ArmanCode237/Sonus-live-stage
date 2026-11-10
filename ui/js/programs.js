// ============================================================================
// ui/js/programs.js — Programas 1-8 (STORE/recall/import/export) y controles de teclado
//   transpose · octave · touch · soft release
// ============================================================================
function initProgramButtons() {
  const grid = $('programsGrid');
  if (!grid) return;
  grid.innerHTML = '';
  for (let i = 0; i < 8; i++) {
    const wrap = document.createElement('div');
    wrap.className = 'prog-slot-wrap';
    wrap.innerHTML = `
      <span class="prog-slot-num">${i + 1}</span>
      <div class="pad-body toggle-pad ${i === 0 ? 'active' : ''}" id="progPad_${i}">
        <div class="toggle-nord-oval"></div>
        <div class="toggle-hw-led"></div>
      </div>
    `;
    wrap.onclick = () => onProgramButtonClick(i);
    grid.appendChild(wrap);
  }
  fetch('/api/programs').then(r => r.json()).then(d => { state.programs = d; }).catch(() => {});
}

function toggleStoreMode(){
  const sel=$('programStoreSlot'),name=$('programStoreName');
  if(sel)sel.value=String(state.activeProgramSlot||0);
  if(name)name.value=state.programs?.[Number(sel?.value||0)]?.name || `PROGRAM ${Number(sel?.value||0)+1}`;
  openModal('modalProgramStore');name?.focus();name?.select();
}
async function commitProgramStore(){
  const slotIdx=Number($('programStoreSlot')?.value||0);
  const name=($('programStoreName')?.value||'').trim();
  if(!name){$('programStoreName')?.focus();return;}
  try{
    const response=await fetch('/api/programs/store',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:slotIdx,name})});
    if(!response.ok)throw new Error(`HTTP ${response.status}`);
    const res=await response.json();if(!res.ok)throw new Error(res.error||'Store failed');
    if(res.programs)state.programs=res.programs;
    setActiveProgramPad(slotIdx);setText('lcdProgramName',res.programName||name);
    closeModal('modalProgramStore');
  }catch(err){const warn=$('programStoreWarning');if(warn)warn.textContent='No fue posible guardar: '+err.message;}
}
async function confirmInitProgram(){
  if(!confirm('¿Inicializar la configuración actual? No se sobrescribirán los programas guardados hasta usar STORE.'))return;
  try{
    const resp=await fetch('/api/programs/init',{method:'POST'});if(!resp.ok)throw new Error(`HTTP ${resp.status}`);
    const res=await resp.json();if(!res.ok)throw new Error(res.error||'Initialization failed');
    if(res.params)syncFromServerParams(res.params);
    if(res.pianoLayers&&typeof updatePianoLayerSources==='function')updatePianoLayerSources(res.pianoLayers);
    setText('lcdProgramName',res.programName||'INIT PERFORMANCE');
  }catch(err){alert('Error al inicializar: '+err.message);}
}
function onProgramButtonClick(slotIdx){
  fetch('/api/programs/recall',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({slot:slotIdx})})
    .then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})
    .then(res=>{
      setActiveProgramPad(slotIdx);
      setText('lcdProgramName',res.programName||'Program');
      // Instrument names are rendered independently for enabled modules.
      if(res.sourcePath)state.currentSourcePath=res.sourcePath;
      if(res.params)syncFromServerParams(res.params);
      if(res.pianoLayers&&typeof updatePianoLayerSources==='function')updatePianoLayerSources(res.pianoLayers);
    }).catch(err=>console.error('[program recall]',err));
}

function setActiveProgramPad(slotIdx) {
  state.activeProgramSlot = slotIdx;
  if (typeof updateMainDashboard==='function') updateMainDashboard();
  for (let i = 0; i < 8; i++) {
    const pad = $(`progPad_${i}`);
    if (pad) pad.classList.toggle('active', i === slotIdx);
  }
}

function downloadProgramsJson() {
  const blob = new Blob([JSON.stringify(state.programs, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'nord_stage_programs.json';
  a.click();
}

function importProgramsFile(inputEl) {
  const file = inputEl.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = e => {
    fetch('/api/programs/import', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: e.target.result
    }).then(r => r.json()).then(res => {
      if (res.ok && res.programs) {
        state.programs = res.programs;
        onProgramButtonClick(0);
      }
    }).catch(() => {});
  };
  reader.readAsText(file);
  inputEl.value = '';   // permite volver a importar el mismo archivo
}

function stepTranspose(dir) {
  state.transpose = Math.max(-12, Math.min(12, state.transpose + dir));
  setText('transposeVal', state.transpose);
  sendParam('transpose', state.transpose);
}
function stepOctave(dir) {
  state.octave = Math.max(-2, Math.min(2, state.octave + dir));
  setText('octaveVal', state.octave);
  sendParam('octave', state.octave);
}
function cycleTouch() {
  state.touch = (state.touch + 1) % 3;
  const labels = ['TOUCH: LIGHT', 'TOUCH: NORM', 'TOUCH: HEAVY'];
  setText('btnTouch', labels[state.touch]);
  sendParam('touch', state.touch);
}
function toggleSoftRelease() {
  state.softRelease = !state.softRelease;
  setActive('hwBtnSoftRel', state.softRelease);
  sendParam('softRelease', state.softRelease ? 1 : 0);
}
