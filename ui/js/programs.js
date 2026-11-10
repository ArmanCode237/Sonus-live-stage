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
      <span class="prog-slot-num">● ${i + 1}</span>
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

function toggleStoreMode() {
  state.storeMode = !state.storeMode;
  const st = $('btnStore'); if (st) st.classList.toggle('blink', state.storeMode);
}

function onProgramButtonClick(slotIdx) {
  if (state.storeMode) {
    state.storeMode = false;
    const st = $('btnStore'); if (st) st.classList.remove('blink');
    fetch('/api/programs/store', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ slot: slotIdx })
    }).then(r => r.json()).then(res => {
      if (res.programs) state.programs = res.programs;
      setActiveProgramPad(slotIdx);
    }).catch(() => {});
  } else {
    fetch('/api/programs/recall', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ slot: slotIdx })
    }).then(r => r.json()).then(res => {
      setActiveProgramPad(slotIdx);
      if (res.instrumentName) setText('lcdSampleName', res.instrumentName);
      if (res.sourcePath) state.currentSourcePath = res.sourcePath;
      if (res.params) syncFromServerParams(res.params);
    }).catch(() => {});
  }
}

function setActiveProgramPad(slotIdx) {
  state.activeProgramSlot = slotIdx;
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
