// ============================================================================
// ui/js/sync.js — Sincronización UI ← servidor C++ (objeto `params` de serializeParams)
//
// REGLA IMPORTANTE: esta función solo ACTUALIZA LA UI. Nunca debe llamar a sendParam(),
// de lo contrario cada sincronización provocaría un POST → paramRev++ → otra sincronización
// (bucle infinito). Por eso selectReverbType/setShimmerMode/setLayerWave reciben `silent=true`.
// Todos los accesos al DOM son tolerantes: cada página tiene solo parte de los elementos.
// ============================================================================

function syncFromServerParams(p) {
  if (!p || state.isDraggingControl) return;

  state.pianoEnabled = p.pianoEnabled !== undefined ? !!p.pianoEnabled : true;
  if(p.pianoLayers && Array.isArray(p.pianoLayers)) {
    state.pianoLayers = p.pianoLayers.map((l,i)=>({enabled:!!l.enabled, volume:Number(l.volume ?? (i===0?1:0.75))}));
  }
  if(Array.isArray(p.pianoSampleConfig)) {
    state.pianoSampleConfig = p.pianoSampleConfig.map(cfg=>({...cfg}));
    if(typeof refreshPianoSampleEditor==='function')refreshPianoSampleEditor();
  }
  if(Array.isArray(p.fxSends)) {
    state.fxSends = Array.from({length:9},(_,i)=>{
      const src = p.fxSends[i] || {};
      return {
        modulation: Number(src.modulation ?? 1),
        delay: Number(src.delay ?? 1),
        reverb: Number(src.reverb ?? 1)
      };
    });
    if(typeof refreshFxSends === 'function') refreshFxSends();
  }
  if(typeof updatePianoLayerControls==='function') updatePianoLayerControls();
  const mv = p.maxVoices || 128;
  const mvSel = $('maxVoicesSelect');
  if (mvSel) mvSel.value = mv;
  setText('maxVoicesTopLabel', mv);

  state.transpose = p.transposeSemis || 0;
  state.octave = p.octaveShift || 0;
  setText('transposeVal', state.transpose);
  setText('octaveVal', state.octave);

  state.softRelease = !!p.softRelease;
  setActive('hwBtnSoftRel', state.softRelease);

  state.touch = p.kbdTouch !== undefined ? p.kbdTouch : 1;
  const tLabels = ['TOUCH: LIGHT', 'TOUCH: NORM', 'TOUCH: HEAVY'];
  setText('btnTouch', tLabels[state.touch] || 'TOUCH: NORM');

  const direct = {
    tune: p.masterTuneCents, volume: p.volume, pianoVolume: p.pianoVolume, pan: p.pan,
    cutoff: p.cutoffHz, resonance: p.resonanceQ,
    attack: p.attackSec, decay: p.decaySec, sustain: p.sustainLevel, release: p.releaseSec, loopHold: p.loopHold,
    lowCut: p.lowCutHz, highCut: p.highCutHz,
    chorusRate: p.chorusRateHz, chorusDepth: p.chorusDepth, chorusMix: p.chorusMix,
    chorusVoices:p.chorusVoices,chorusWidth:p.chorusWidth,chorusFeedback:p.chorusFeedback,chorusTone:p.chorusTone,chorusDelay:p.chorusDelay,
    phaserRate: p.phaserRateHz, phaserDepth: p.phaserDepth, phaserFeedback: p.phaserFeedback, phaserMix: p.phaserMix,
    tremoloRate: p.tremoloRateHz, tremoloDepth: p.tremoloDepth,
    delayTimeMs: p.delayTimeMs, delayFeedback: p.delayFeedback, delayDamping: p.delayDamping, delayMix: p.delayMix,
    reverbMix: p.reverbMix, reverbSize: p.reverbSize, reverbDamping: p.reverbDamping, reverbPreDelayMs: p.reverbPreDelayMs, reverbShimmer: p.reverbShimmer,
    shimmerShiftSt: p.shimmerShiftSt, shimmerFeedback: p.shimmerFeedback, shimmerDiffusion: p.shimmerDiffusion,
    shimmerLowCutHz: p.shimmerLowCutHz, shimmerHighCutHz: p.shimmerHighCutHz,
    shimmerModRateHz: p.shimmerModRateHz, shimmerModDepth: p.shimmerModDepth,
    synthMasterVol: p.synthMasterVol, synthMacroCutoff: p.synthMacroCutoff,
    synthLfoRate: p.synthLfoRate, synthLfoDepth: p.synthLfoDepth,
    synthLfoPitch: p.synthLfoPitch, synthLfoCutoff: p.synthLfoCutoff,
    organVolume: p.organVolume, organDrive: p.organDrive,
    organRotaryMix: p.organRotaryMix, organVibratoDepth:p.organVibratoDepth,
    organVibratoRate:p.organVibratoRate,
    organChorusRate:p.organChorusRate,organChorusDepth:p.organChorusDepth,organChorusMix:p.organChorusMix,
    organChorusWidth:p.organChorusWidth,organChorusFeedback:p.organChorusFeedback,organChorusTone:p.organChorusTone,organChorusVoices:p.organChorusVoices,
    organPercDecaySec: p.organPercDecaySec, organPercLevel: p.organPercLevel
  };
  Object.keys(direct).forEach(k => {
    if (direct[k] !== undefined) {
      state[k] = direct[k];
      updateAllControlsForId(k, direct[k]);
    }
  });

  if (p.eqBands) {
    for (let i = 0; i < 5; i++) {
      state[`eqFreq${i}`] = p.eqBands[i].freqHz;
      state[`eqQ${i}`]    = p.eqBands[i].q;
      state[`eqGain${i}`] = p.eqBands[i].gainDb;
      updateAllControlsForId(`eqFreq${i}`, p.eqBands[i].freqHz);
      updateAllControlsForId(`eqQ${i}`, p.eqBands[i].q);
      updateAllControlsForId(`eqGain${i}`, p.eqBands[i].gainDb);
    }
  }

  if (p.synthPresetId !== undefined && typeof showSynthPreset === 'function') showSynthPreset(p.synthPresetId);

  if (p.synthLfoEnabled !== undefined) {
    state.synthLfoEnabled=!!p.synthLfoEnabled;
    setSynthLfoEnabled(true);
  }
  if (p.synthLfoWave !== undefined) setSynthLfoWave(p.synthLfoWave,true);

  if (p.synthLayers) {
    for (let i = 0; i < 3; i++) {
      const L = p.synthLayers[i];
      const n = i + 1;
      state[`L${n}_en`] = !!L.enabled;
      updateSynthLayerBadges(n, state[`L${n}_en`]);
      if (L.waveType !== undefined)   setLayerWave(n, L.waveType, true);
      if (L.filterType !== undefined) setLayerFilterType(n, L.filterType, true);
      if (L.osc2Wave !== undefined) setSynthOsc2Wave(n,L.osc2Wave,true);
      const lMap = {
        vol: L.volume, pan: L.pan, oct: L.octave, semi: L.semitones, det: L.detuneCents,
        cut: L.cutoffHz, res: L.resonanceQ, atk: L.attackSec, dec: L.decaySec, sus: L.sustainLevel, rel: L.releaseSec,
        unison: L.unisonVoices, uniDet: L.unisonDetune, uniSprd: L.unisonSpread,
        filtEnv: L.filterEnvAmt, fAtk: L.filtAttackSec, fDec: L.filtDecaySec, fSus: L.filtSustainLvl, fRel: L.filtReleaseSec,
        saw:L.sawLevel, pulse:L.pulseLevel, pw:L.pulseWidth, pwmDepth:L.pwmDepth,
        pwmRate:L.pwmRateHz, sub:L.subLevel, noise:L.noiseLevel,
        osc2:L.osc2Level, osc2Det:L.osc2Detune, osc2Semi:L.osc2Semitones,
        hpf:L.highpassHz, drive:L.drive, filtKT:L.filterKeyTrack,
        velCut:L.velToCutoff, wtIdx:L.wavetableIdx
      };
      Object.keys(lMap).forEach(sub => {
        if (lMap[sub] !== undefined) {
          state[`L${n}_${sub}`] = lMap[sub];
          updateAllControlsForId(`L${n}_${sub}`, lMap[sub]);
        }
      });
    }
  }

  if (p.drawbars) {
    for (let d = 0; d < 9; d++) {
      state[`drawbar${d}`] = p.drawbars[d];
      updateDrawbarVisual(d, p.drawbars[d]);
    }
  }

  state.synthEnabled = !!p.synthEnabled;
  state.organChorusEnabled = !!p.organChorusEnabled;
  setActive('led_organChorusEnabled',state.organChorusEnabled);
  setActive('ledOrgan_organChorusEnabled',state.organChorusEnabled);
  state.organEnabled = !!p.organEnabled;
  state.organRotaryFast = !!p.organRotaryFast;
  state.organModel = p.organModel || 0;
  state.organVibratoMode = p.organVibratoMode || 0;
  state.organPercEnabled = !!p.organPercEnabled;
  state.organPercHarmonic = p.organPercHarmonic || 3;

  updateOrganControlsVisual();

  state.eqEnabled = !!p.eqEnabled;
  state.chorusEnabled = !!p.chorusEnabled;
  state.phaserEnabled = !!p.phaserEnabled;
  state.tremoloEnabled = !!p.tremoloEnabled;
  state.delayEnabled = !!p.delayEnabled;
  if (p.delayPingPong !== undefined) { state.delayPingPong = !!p.delayPingPong; setActive('pillPingPong', state.delayPingPong); }
  state.reverbEnabled = !!p.reverbEnabled;

  selectReverbType(p.reverbType !== undefined ? p.reverbType : 2, true);   // true = solo UI
  if (p.shimmerPitchMode  !== undefined) setShimmerMode('shimmerPitchMode',  p.shimmerPitchMode,  true);
  if (p.shimmerReverbMode !== undefined) setShimmerMode('shimmerReverbMode', p.shimmerReverbMode, true);
  if (p.shimmerColorMode  !== undefined) setShimmerMode('shimmerColorMode',  p.shimmerColorMode,  true);
  updateModLeds();
  drawEqCurve();
}
