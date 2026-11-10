// ============================================================================
// ui/js/polling.js — Lectura periódica de /api/state (VU, voces, MIDI, teclas, parámetros)
//
// Mejoras respecto a setInterval(…, 55):
//   · bucle con setTimeout: NO lanza una petición nueva hasta recibir la anterior
//     (evita acumular peticiones si el servidor tarda)
//   · si el servidor no responde, reintenta más despacio (1 s)
//   · se pausa cuando la pestaña no está visible
//   · solo se re-sincroniza la UI cuando cambia paramRev / instrumento
// ============================================================================

const POLL_INTERVAL_MS = 60;
const POLL_RETRY_MS = 1000;

function startWorkstationPolling() {
  let lastMidiSeq = 0;

  const applyState = s => {
    if (s.instrumentId !== state.currentInstId || s.paramRev !== state.lastParamRev) {
      state.currentInstId = s.instrumentId;
      state.lastParamRev = s.paramRev;
      state.currentSourcePath = s.sourcePath || state.currentSourcePath;
      setText('lcdSampleName', s.instrumentName || 'Synth / Organ Stage');
      setText('zoneCountNum', s.zoneCount || 0);
      setText('ramTopText', (s.memoryMB || 0).toFixed(1) + ' MB');
      if (s.activeProgramSlot !== undefined) setActiveProgramPad(s.activeProgramSlot);
      if (s.params) syncFromServerParams(s.params);
    }

    setText('activeVoicesText', s.activeVoices);
    const vuL = $('vuL'), vuR = $('vuR');
    if (vuL) vuL.style.width = Math.min(100, Math.pow(s.peakL || 0, 0.5) * 100) + '%';
    if (vuR) vuR.style.width = Math.min(100, Math.pow(s.peakR || 0, 0.5) * 100) + '%';
    const clip = $('clipLed');
    if (clip) clip.classList.toggle('on', !!s.clipping);
    setText('pedalBadgeTop', s.pedalDown ? 'Pedal: DOWN' : 'Pedal: UP');

    const hasHwWarning = (s.disconnectedMidiCount > 0) || !!s.savedAudioMissing;
    const setupBtn = $('btnAudioMidiTop');
    if (setupBtn) setupBtn.classList.toggle('warn-alert', hasHwWarning);

    const midiBadge = $('midiConnectionBadge');
    if (midiBadge) {
      if (s.disconnectedMidiCount > 0) {
        midiBadge.className = 'hw-badge warn';
        midiBadge.innerText = `⚠ MIDI DESCONECTADO (${s.disconnectedMidiCount})`;
      } else if (s.connectedMidiCount > 0) {
        midiBadge.className = 'hw-badge';
        midiBadge.innerText = `MIDI ACTIVO (${s.connectedMidiCount})`;
      } else {
        midiBadge.className = 'hw-badge off';
        midiBadge.innerText = 'MIDI: SIN SELECCIÓN';
      }
    }

    if (s.midiSeq && s.midiSeq !== lastMidiSeq) {
      lastMidiSeq = s.midiSeq;
      setText('midiStatusTop', `Note #${s.lastMidiNote} (Vel:${s.lastMidiVel})`);
    }

    if (state.keyboardVisible) {
      const keyMap = new Map();
      (s.activeKeys || []).forEach(k => keyMap.set(k.k, k.s));
      for (let m = 21; m <= 108; m++) {
        const el = $(`vkey_${m}`);
        if (!el) continue;
        const st = keyMap.get(m) || 0;
        el.classList.toggle('held', st === 1 || state.mousePressedNote === m);
        el.classList.toggle('sustained', st === 2 && state.mousePressedNote !== m);
      }
    }
  };

  const tick = () => {
    if (document.hidden) { setTimeout(tick, 250); return; }
    fetch('/api/state')
      .then(r => r.json())
      .then(s => { applyState(s); setTimeout(tick, POLL_INTERVAL_MS); })
      .catch(() => setTimeout(tick, POLL_RETRY_MS));
  };
  tick();
}
