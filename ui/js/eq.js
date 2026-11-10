// ============================================================================
// ui/js/eq.js — Ecualizador paramétrico de 5 bandas (modal) + curva de respuesta
// Solo existe en index.html (modal-eq.html); en otras páginas las funciones son no-op.
// ============================================================================
function buildParametricEqModal() {
  const box = $('paramEqBandsContainer');
  if (!box) return;
  box.innerHTML = '';
  for (let b = 0; b < 5; b++) {
    let leds = '';
    for (let s = 0; s < 11; s++) leds += `<div class="fader-led-seg" id="eqLed_${b}_${s}"></div>`;
    const card = document.createElement('div');
    card.className = 'eq-band-card';
    card.innerHTML = `
      <div class="eq-band-title">${BAND_NAMES[b]}</div>
      <div class="knob-cell" data-id="eqFreq${b}" data-min="20" data-max="20000" data-val="${DEFAULT_FREQS[b]}" data-log="1" data-unit="Hz" data-label="FREQ"></div>
      <div class="knob-cell" data-id="eqQ${b}" data-min="0.25" data-max="12.0" data-val="1.0" data-log="1" data-unit="Q" data-label="FACTOR Q"></div>
      <div class="fader-body" id="eqFaderBody_${b}">
        <div class="fader-ticks"></div>
        <div class="fader-track"><div class="fader-thumb" id="eqThumb_${b}"></div></div>
        <div class="fader-led-strip">${leds}</div>
      </div>
      <div class="knob-readout" id="eqFaderVal_${b}">+0.0 dB</div>
    `;
    box.appendChild(card);
    const fBody = card.querySelector('.fader-body');
    fBody.style.touchAction = 'none';
    fBody.addEventListener('pointerdown', e => {
      state.isDraggingControl = true;
      const rect = fBody.getBoundingClientRect();
      const onMove = ev => {
        const norm = 1.0 - Math.max(0, Math.min(1, (ev.clientY - rect.top) / rect.height));
        const db = Math.round(((norm * 30) - 15) * 2) / 2;
        state[`eqGain${b}`] = db;
        updateAllControlsForId(`eqGain${b}`, db);
        sendParam(`eqGain${b}`, db);
        drawEqCurve();
      };
      onMove(e);
      const onUp = () => { state.isDraggingControl = false; window.removeEventListener('pointermove', onMove); window.removeEventListener('pointerup', onUp); };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
    });
  }
}

function updateEqFaderVisual(b, db) {
  const thumb = $(`eqThumb_${b}`);
  if (!thumb) return;
  const norm = Math.max(0, Math.min(1, (db + 15) / 30));
  thumb.style.bottom = `${norm * 100}%`;
  $(`eqFaderVal_${b}`).innerText = (db >= 0 ? "+" : "") + db.toFixed(1) + " dB";
  const lit = Math.round(norm * 11);
  for (let s = 0; s < 11; s++) { const led = $(`eqLed_${b}_${s}`); if (led) led.classList.toggle('lit', s < lit); }
}

function resetParametricEq() {
  for (let b = 0; b < 5; b++) {
    state[`eqFreq${b}`] = DEFAULT_FREQS[b];
    state[`eqQ${b}`] = 1.0;
    state[`eqGain${b}`] = 0.0;
    updateAllControlsForId(`eqFreq${b}`, DEFAULT_FREQS[b]);
    updateAllControlsForId(`eqQ${b}`, 1.0);
    updateAllControlsForId(`eqGain${b}`, 0.0);
    sendParam(`eqFreq${b}`, DEFAULT_FREQS[b]);
    sendParam(`eqQ${b}`, 1.0);
    sendParam(`eqGain${b}`, 0.0);
  }
  drawEqCurve();
}

function drawEqCurve() {
  const c = $('eqGraphCanvas');
  if (!c) return;
  const ctx = c.getContext('2d');
  const w = c.width, h = c.height, midY = h / 2;
  ctx.clearRect(0, 0, w, h);
  ctx.strokeStyle = '#1e293b';
  ctx.beginPath(); ctx.moveTo(0, midY); ctx.lineTo(w, midY); ctx.stroke();

  const xToFreq = x => 20 * Math.pow(20000 / 20, x / w);
  ctx.beginPath();
  for (let x = 0; x <= w; x += 3) {
    const f = xToFreq(x);
    let db = 0;
    for (let b = 0; b < 5; b++) {
      const bf = state[`eqFreq${b}`], bq = Math.max(0.25, state[`eqQ${b}`]), bg = state[`eqGain${b}`];
      const octDist = Math.log2(f / bf), bw = 1.0 / bq;
      db += bg * Math.exp(-(octDist * octDist) / (0.35 * bw * bw));
    }
    if (f < state.lowCut) db -= 12 * Math.log2(state.lowCut / f);
    if (f > state.highCut) db -= 12 * Math.log2(f / state.highCut);
    const y = Math.max(6, Math.min(h - 6, midY - (db / 18) * (h * 0.44)));
    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = '#38bdf8'; ctx.lineWidth = 2.5; ctx.stroke();
}
