// ============================================================================
// ui/js/knobs.js — Motor de perillas SVG
//   · arrastre vertical (ratón o táctil), doble clic = valor por defecto
//   · cualquier <div class="knob-cell" data-id data-min data-max ...> se convierte en perilla
// Mientras se arrastra, state.isDraggingControl = true para que el polling
// no pise el valor que el usuario está moviendo.
// ============================================================================
// ============================================================================
// MOTOR DE PERILLAS SVG (Con soporte para arrastre y doble clic a valor base)
// ============================================================================
function buildNordTicksSvg() {
  let lines = '';
  for (let i = 0; i < 21; i++) {
    const rad = (-135 + (i / 20) * 270) * (Math.PI / 180);
    const major = (i % 2 === 0), r1 = major ? 28 : 30, r2 = 35;
    lines += `<line class="${major ? 'nord-tick-major' : 'nord-tick-minor'}" x1="${(40 + Math.sin(rad)*r1).toFixed(1)}" y1="${(40 - Math.cos(rad)*r1).toFixed(1)}" x2="${(40 + Math.sin(rad)*r2).toFixed(1)}" y2="${(40 - Math.cos(rad)*r2).toFixed(1)}"/>`;
  }
  return lines;
}

function valToNorm(cell, val) {
  const min = parseFloat(cell.dataset.min), max = parseFloat(cell.dataset.max);
  return cell.dataset.log === "1"
    ? Math.log(Math.max(min, val) / min) / Math.log(max / min)
    : (val - min) / (max - min);
}

function normToVal(cell, norm) {
  const min = parseFloat(cell.dataset.min), max = parseFloat(cell.dataset.max);
  let v = cell.dataset.log === "1" ? min * Math.pow(max / min, norm) : min + norm * (max - min);
  if (cell.dataset.step === "1") v = Math.round(v);
  else if (cell.dataset.stepSize) {
    const step = Number(cell.dataset.stepSize);
    if (step > 0) v = Math.round(v / step) * step;
  }
  return Math.min(max,Math.max(min,v));
}

function formatReadout(val, unit) {
  if (unit === "Hz") return val >= 1000 ? (val / 1000).toFixed(2) + " kHz" : Math.round(val) + " Hz";
  if (unit === "s") return val < 0.1 ? Math.round(val * 1000) + " ms" : val.toFixed(2) + " s";
  if (unit === "%") return Math.round(val * 100) + " %";
  if (unit === "dB") return (val >= 0 ? "+" : "") + val.toFixed(1) + " dB";
  if (unit === "Hold") return val >= 0.98 ? "∞ HOLD" : Math.round(val * 100) + " %";
  if (unit === "Pan") return Math.abs(val) < 0.04 ? "CENTER" : (val < 0 ? "L" + Math.round(-val * 100) : "R" + Math.round(val * 100));
  if (unit === "st") return (val >= 0 ? "+" : "") + Math.round(val) + " st";
  if (unit === "oct") return (val >= 0 ? "+" : "") + Math.round(val) + " oct";
  if (unit === "int") return String(Math.round(val));
  return Number(val).toFixed(2) + " " + (unit || "");
}

function updateSingleKnobCell(cell, val) {
  const norm = Math.max(0, Math.min(1, valToNorm(cell, val)));
  const ptr = cell.querySelector('.knob-pointer');
  const ro  = cell.querySelector('.knob-readout');
  if (ptr) ptr.setAttribute('transform', `rotate(${-135 + norm * 270} 40 40)`);
  if (ro)  ro.innerText = formatReadout(val, cell.dataset.unit);
  const svg=cell.querySelector('.knob-svg');
  if (svg) {
    svg.setAttribute('aria-valuenow', String(Number(val.toFixed(4))));
    svg.setAttribute('aria-valuetext',formatReadout(val,cell.dataset.unit));
  }
}

function updateAllControlsForId(id, val) {
  document.querySelectorAll(`.knob-cell[data-id="${id}"]`).forEach(c => updateSingleKnobCell(c, val));
  if (id.startsWith('eqGain') && typeof updateMainEqSlider === 'function') {
    updateMainEqSlider(parseInt(id.replace('eqGain','')), val);
  }
  if (/^pianoLayerVol[0-4]$/.test(id)) {
    const layer=Number(id.slice(-1));
    if (state.pianoLayers?.[layer]) state.pianoLayers[layer].volume=Number(val);
    setText(`pianoLayerVal${layer}`,`${Math.round(val*100)}%`);
  }
  if (/^fxSend[0-8](Mod|Delay|Reverb)$/.test(id) && typeof updateFxSendKnob === 'function') updateFxSendKnob(id,val);
  if (/^pianoSample[0-4](Atk|Dec|Sus|Rel|Hold|Pan|Tune|Cut|Res)$/.test(id) && typeof updateSampleLayerParameter==='function') updateSampleLayerParameter(id,val);
  if (id.startsWith('eqGain') && typeof updateEqFaderVisual === 'function') {
    updateEqFaderVisual(parseInt(id.replace('eqGain', '')), val);
  }
}

function initAllKnobs() {
  const ticks = buildNordTicksSvg();
  document.querySelectorAll('.knob-cell').forEach((cell, index) => {
    if (cell.dataset.knobInitialized === '1') return;
    cell.dataset.knobInitialized = '1';
    const id = cell.dataset.id;
    const sizeCls = cell.dataset.large === "1" ? "large" : (cell.dataset.small === "1" ? "small" : "");
    // Definiciones únicas: SVG robusto aun con muchas perillas en la misma vista.
    const metalId=`metal-${index}`, faceId=`face-${index}`;
    let grip='';
    for (let n=0;n<20;n++) {
      const a=n*Math.PI/10;
      grip+=`<line class="knob-grip" x1="${(40+21*Math.cos(a)).toFixed(2)}" y1="${(40+21*Math.sin(a)).toFixed(2)}" x2="${(40+25*Math.cos(a)).toFixed(2)}" y2="${(40+25*Math.sin(a)).toFixed(2)}"/>`;
    }
    cell.innerHTML = `
      <svg class="knob-svg ${sizeCls}" viewBox="0 0 80 80" tabindex="0" role="slider"
        aria-label="${cell.dataset.label || id}" aria-valuemin="${cell.dataset.min}" aria-valuemax="${cell.dataset.max}">
        <defs>
          <linearGradient id="${metalId}" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0%" stop-color="#747b7c"/><stop offset="23%" stop-color="#272d31"/>
            <stop offset="60%" stop-color="#111719"/><stop offset="100%" stop-color="#545c5e"/>
          </linearGradient>
          <radialGradient id="${faceId}" cx="37%" cy="24%" r="88%">
            <stop offset="0%" stop-color="#535b5b"/><stop offset="42%" stop-color="#22282c"/>
            <stop offset="100%" stop-color="#080d0f"/>
          </radialGradient>
        </defs>
        <g class="knob-nord-ticks">${ticks}</g>
        <circle cx="40" cy="41" r="27" fill="#020607" opacity=".72"/>
        <circle class="knob-outer" cx="40" cy="40" r="27" fill="url(#${metalId})"/>
        <circle class="knob-cap" cx="40" cy="40" r="23" fill="url(#${faceId})"/>
        <g>${grip}</g>
        <circle class="knob-bevel" cx="40" cy="40" r="19.6"/>
        <line class="knob-pointer" x1="40" y1="39" x2="40" y2="18"/>
      </svg>
      <div class="knob-title">${cell.dataset.label || id}</div>
      <div class="knob-readout"></div>
    `;
    const svg = cell.querySelector('svg');
    const initVal = state[id] !== undefined ? state[id] : parseFloat(cell.dataset.val);
    state[id] = initVal;
    updateSingleKnobCell(cell, initVal);

    svg.style.touchAction = 'none';
    svg.addEventListener('keydown', e => {
      const min=Number(cell.dataset.min),max=Number(cell.dataset.max);
      const current=state[id]===undefined ? Number(cell.dataset.val) : Number(state[id]);
      const currentNorm=Math.max(0,Math.min(1,valToNorm(cell,current)));
      let targetNorm;
      if (e.key==='ArrowUp'||e.key==='ArrowRight') targetNorm=currentNorm+.01;
      else if(e.key==='ArrowDown'||e.key==='ArrowLeft') targetNorm=currentNorm-.01;
      else if(e.key==='PageUp') targetNorm=currentNorm+.10;
      else if(e.key==='PageDown') targetNorm=currentNorm-.10;
      else if(e.key==='Home')targetNorm=0;
      else if(e.key==='End')targetNorm=1;
      else return;
      e.preventDefault();
      let v=normToVal(cell,Math.max(0,Math.min(1,targetNorm)));
      state[id]=v;
      updateAllControlsForId(id,v);
      sendParam(id,v);
      if ((id.startsWith('eq') || id==='lowCut' || id==='highCut') && typeof drawEqCurve==='function') drawEqCurve();
    });
    svg.addEventListener('pointerdown', e => {
      e.preventDefault();
      state.isDraggingControl = true;
      const startY = e.clientY, curId = cell.dataset.id;
      const curVal = state[curId] !== undefined ? state[curId] : parseFloat(cell.dataset.val);
      const startNorm = valToNorm(cell, curVal);
      const onMove = ev => {
        const norm = Math.max(0, Math.min(1, startNorm + (startY - ev.clientY) / 130));
        const val = normToVal(cell, norm);
        state[curId] = val;
        updateAllControlsForId(curId, val);
        sendParam(curId, val);
        if ((curId.startsWith('eq') || curId === 'lowCut' || curId === 'highCut') && typeof drawEqCurve === 'function') {
          drawEqCurve();
        }
      };
      const onUp = () => {
        state.isDraggingControl = false;
        window.removeEventListener('pointermove', onMove);
        window.removeEventListener('pointerup', onUp);
        window.removeEventListener('pointercancel', onUp);
      };
      window.addEventListener('pointermove', onMove);
      window.addEventListener('pointerup', onUp);
      window.addEventListener('pointercancel', onUp);
    });

    svg.addEventListener('dblclick', () => {
      const curId = cell.dataset.id;
      const defVal = parseFloat(cell.dataset.val);
      state[curId] = defVal;
      updateAllControlsForId(curId, defVal);
      sendParam(curId, defVal);
    });
  });
}
