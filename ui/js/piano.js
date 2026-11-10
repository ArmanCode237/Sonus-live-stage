// Piano sample layers. State changes go through /api/param, bank changes through /api/load.
function selectPianoLayer(layer) {
  state.activePianoLayer = Math.max(0,Math.min(4,Number(layer)||0));
  updatePianoLayerControls();
  if(typeof renderBrowserList==='function')renderBrowserList();
}
function togglePianoLayer(layer) {
  if(!Array.isArray(state.pianoLayers)) return;
  state.pianoLayers[layer].enabled = !state.pianoLayers[layer].enabled;
  updatePianoLayerControls();
  sendParam('pianoLayerEnabled'+layer,state.pianoLayers[layer].enabled?1:0);
}
function changePianoLayerVolume(layer,value) {
  if(!Array.isArray(state.pianoLayers)) return;
  const volume = Math.min(1.5,Math.max(0,Number(value)));
  state.pianoLayers[layer].volume=volume;
  const output=$('pianoLayerVal'+layer);
  if(output) output.textContent=Math.round(volume*100)+'%';
  updateAllControlsForId('pianoLayerVol'+layer, volume);
  sendParam('pianoLayerVol'+layer,volume);
}
function updatePianoLayerSources(sources) {
  if(!Array.isArray(sources)) return;
  state.pianoLayerSources = sources.map((v)=>({
    name:(v&&v.sourcePath) ? (v.name||'Piano') : 'VACÍA',
    sourcePath:v?.sourcePath||''
  }));
  updatePianoLayerControls();
}
function updatePianoLayerControls() {
  const layer=state.activePianoLayer||0;
  for(let i=0;i<5;i++){
    const p=state.pianoLayers?.[i]||{enabled:i===0,volume:i===0?1:0.75};
    const bank=state.pianoLayerSources?.[i]||{name:'VACÍA',sourcePath:''};
    const row=$('pianoLayerRow'+i);
    if(row)row.classList.toggle('is-selected',layer===i);
    const led=$('pianoLayerLed'+i);
    if(led)led.classList.toggle('on',!!p.enabled);
    const label=$('pianoLayerName'+i);
    if(label){label.textContent=bank.name;label.title=bank.sourcePath||'Sin sonido';}
    // Las capas utilizan la misma perilla SVG que los demás parámetros.
    if (typeof updateAllControlsForId==='function') updateAllControlsForId('pianoLayerVol'+i,p.volume);
    const output=$('pianoLayerVal'+i);
    if(output)output.textContent=Math.round(p.volume*100)+'%';
  }
  setText('pianoLayerHint',`BROWSER → cargar en L${layer+1}`);
}
async function clearPianoLayer(layer) {
  try {
    const r=await fetch('/api/piano/layers/clear',{
      method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({layer})
    });
    const result=await r.json();
    if(!r.ok || !result.ok)throw new Error(result.error||'No se pudo vaciar la capa');
    if(result.params)syncFromServerParams(result.params);
    updatePianoLayerSources(result.pianoLayers);
  }catch(e){console.error('[Piano layer]',e);}
}
function adjustMainEqGain(band,val) {
  const gain=Number(val);
  if(!Number.isFinite(gain))return;
  state['eqGain'+band]=gain;
  updateAllControlsForId('eqGain'+band,gain);
  sendParam('eqGain'+band,gain);
}
function updateMainEqSlider(band,val) {
  const output=$('eqMainValue'+band);
  if(output)output.textContent=(val>=0?'+':'')+Number(val).toFixed(1)+' dB';
}
