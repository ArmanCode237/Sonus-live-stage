// Stereo parallel FX buses: Modulation / Delay / Reverb, one amount per source.
// IDs map one-to-one to registered C++ parameters fxSend{0..8}{Mod|Delay|Reverb}.
const FX_SEND_SOURCES = [
  'PIANO L1','PIANO L2','PIANO L3','PIANO L4','PIANO L5',
  'SYNTH L1','SYNTH L2','SYNTH L3','ORGAN'
];
const FX_SEND_BUSES = [
  ['Mod','modulation'],['Delay','delay'],['Reverb','reverb']
];
function buildFxSendMatrix() {
  const el = $('fxSendsRows');
  if (!el) return;
  // Static HTML is intentionally supplied in the modal, so its controls never disappear
  // when an async builder runs before the partial loader, or a page is restored from cache.
  if (el.querySelectorAll('.fx-send-knob').length === FX_SEND_SOURCES.length*FX_SEND_BUSES.length) return;
  el.innerHTML = FX_SEND_SOURCES.map((name,i)=>`
    <div class="fx-send-row">
      <div class="fx-send-source"><span class="fx-send-source-index">${String(i+1).padStart(2,'0')}</span><strong>${name}</strong></div>
      ${FX_SEND_BUSES.map(([bus,key])=>`<div class="fx-send-control">
        <div class="knob-cell fx-send-knob" data-id="fxSend${i}${bus}" data-min="0" data-max="1" data-val="1" data-step-size="0.01" data-log="0" data-label="${bus.toUpperCase()}" data-unit="%" data-small="1"></div>
      </div>`).join('')}
    </div>`).join('');
}
function updateFxSendKnob(id,value) {
  const m=/^fxSend([0-8])(Mod|Delay|Reverb)$/.exec(id);
  if (!m) return;
  const key=FX_SEND_BUSES.find(([bus])=>bus===m[2])?.[1];
  if (key && state.fxSends[m[1]]) state.fxSends[m[1]][key] = Number(value);
}
function refreshFxSends() {
  if (!Array.isArray(state.fxSends)) return;
  state.fxSends.forEach((src,i)=>FX_SEND_BUSES.forEach(([bus,key])=>{
    const id=`fxSend${i}${bus}`, value=Number(src[key] ?? 1);
    state[id]=value;
    document.querySelectorAll(`.fx-send-knob[data-id="${id}"]`).forEach(knob=>{
      if(knob.querySelector('.knob-pointer')) updateSingleKnobCell(knob,value);
    });
  }));
}
function resetFxSends() {
  FX_SEND_SOURCES.forEach((_,i)=>FX_SEND_BUSES.forEach(([bus,key])=>{
    const id=`fxSend${i}${bus}`;
    state.fxSends[i][key]=1;
    state[id]=1;
    updateAllControlsForId(id,1);
    sendParam(id,1);
  }));
}
