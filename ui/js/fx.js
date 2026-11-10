// ============================================================================
// ui/js/fx.js — Efectos: filtro SVF, pestañas de modulación, reverb y shimmer, delay
// ============================================================================
function resetFilterCutoff() {
  state.cutoff = 20000;
  state.resonance = 0.71;
  updateAllControlsForId('cutoff', 20000);
  updateAllControlsForId('resonance', 0.71);
  sendParam('cutoff', 20000);
  sendParam('resonance', 0.71);
}

function selectModTab(tab) {
  state.activeModTab = tab;
  ['chorus', 'phaser', 'tremolo'].forEach(t => {
    const el = $(`modTab_${t}`);
    if (el) el.classList.toggle('active', t === tab);
  });
  const mixCell = document.querySelector('#mainModMixContainer .knob-cell');
  const paramMap = { chorus: 'chorusMix', phaser: 'phaserMix', tremolo: 'tremoloDepth' };
  const targetParam = paramMap[tab];
  if (mixCell) {
    mixCell.dataset.id = targetParam;
    mixCell.querySelector('.knob-title').innerText = tab.toUpperCase() + " MIX";
    updateSingleKnobCell(mixCell, state[targetParam]);
  }
  updateModLeds();
}
function toggleActiveModEffect() { toggleEffect(`${state.activeModTab}Enabled`); }

// silent=true → solo actualiza la UI (usado al sincronizar desde el servidor).
// Sin 'silent' enviaría reverbType de vuelta y provocaría un bucle sync↔servidor.
function selectReverbType(t, silent = false) {
  state.reverbType = t;
  [0, 1, 2, 3].forEach(i => {
    const p1 = $(`revType_${i}`);
    const p2 = $(`modalRevType_${i}`);
    if (p1) p1.classList.toggle('active', i === t);
    if (p2) p2.classList.toggle('active', i === t);
  });

  const mainTitle = $('reverbMainHeaderTitle');
  if (mainTitle) {
    const names = ['ROOM', 'PLATE', 'HALL', 'SHIMMER'];
    mainTitle.innerText = names[t] || 'REVERB';
  }

  const classicPanel = $('classicReverbPanel');
  const shimmerPanel = $('shimmerReverbPanel');
  if (classicPanel && shimmerPanel) {
    if (t === 3) {
      classicPanel.style.display = 'none';
      shimmerPanel.style.display = 'flex';
    } else {
      classicPanel.style.display = 'flex';
      shimmerPanel.style.display = 'none';

      const subtitles = [
        'ROOM ACÚSTICO — CÁMARA CERCANA DE MADERA',
        'STUDIO PLATE — PLACA METÁLICA BRILLANTE',
        'CONCERT HALL — SALA SINFÓNICA AMPLIA'
      ];
      const sizeNames = ['ROOM DIMENSION', 'PLATE DECAY', 'HALL DIMENSION'];
      const dampNames = ['WOOD DAMPING', 'PLATE DAMPING', 'AIR DAMPING'];

      setText('classicReverbSubtitle', subtitles[t]);
      const sizeTitle = document.querySelector('#classicKnobSize .knob-title');
      const dampTitle = document.querySelector('#classicKnobDamp .knob-title');
      if (sizeTitle) sizeTitle.innerText = sizeNames[t];
      if (dampTitle) dampTitle.innerText = dampNames[t];
      const halo = $('classicKnobHalo');
      if (halo) halo.style.display = (t === 2) ? 'flex' : 'none';
    }
  }
  if (!silent) sendParam('reverbType', t);
}

function setShimmerMode(paramName, modeVal, silent = false) {
  state[paramName] = modeVal;
  const prefixMap = {
    shimmerPitchMode: { pref: 'shmPitch_', count: 5 },
    shimmerReverbMode: { pref: 'shmSpace_', count: 4 },
    shimmerColorMode: { pref: 'shmColor_', count: 2 }
  };
  const info = prefixMap[paramName];
  if (info) {
    for (let i = 0; i < info.count; i++) {
      const el = $(`${info.pref}${i}`);
      if (el) el.classList.toggle('active', i === modeVal);
    }
  }
  if (!silent) sendParam(paramName, modeVal);
}

function setQuickShift(st) {
  state.shimmerShiftSt = st;
  updateAllControlsForId('shimmerShiftSt', st);
  sendParam('shimmerShiftSt', st);
}

function togglePingPong() {
  state.delayPingPong = !state.delayPingPong;
  setActive('pillPingPong', state.delayPingPong);
  sendParam('delayPingPong', state.delayPingPong ? 1 : 0);
}
