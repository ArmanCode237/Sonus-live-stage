// Per-piano-layer sample ADSR controls; IDs correspond directly to C++ registry parameters.
const PIANO_SAMPLE_KEYS = [['Atk','attackSec',.005],['Dec','decaySec',3.5],['Sus','sustainLevel',1],
  ['Rel','releaseSec',.35],['Hold','loopHold',.65],['Pan','pan',0],['Tune','fineTuneCents',0],['Cut','cutoffHz',20000],['Res','resonanceQ',0.707]];
let editingPianoSampleLayer = 0;
function openPianoSampleEditor(layer=state.activePianoLayer||0){
  setSampleEditorLayer(layer);
  openModal('modalPianoSample');
  if(typeof initAllKnobs==='function')initAllKnobs();
  refreshPianoSampleEditor();
  if (typeof refreshFxSends==='function') refreshFxSends();
}
function setSampleEditorLayer(layer){
  editingPianoSampleLayer=Math.max(0,Math.min(4,Number(layer)||0));
  for(let i=0;i<5;i++){
    const panel=$('pianoSamplePanel'+i), tab=$('pianoSampleTab'+i);
    if(panel)panel.hidden=i!==editingPianoSampleLayer;
    if(tab){ tab.classList.toggle('active',i===editingPianoSampleLayer);tab.setAttribute('aria-selected',String(i===editingPianoSampleLayer));}
  }
  refreshPianoSampleEditor();
  if (typeof refreshFxSends==='function') refreshFxSends();
}
function refreshPianoSampleEditor(){
  if(!Array.isArray(state.pianoSampleConfig))return;
  for(let i=0;i<5;i++){
    const config=state.pianoSampleConfig[i]||{};
    const bank=state.pianoLayerSources?.[i];
    setText('pianoSampleLayerName'+i,bank?.name||'SIN SONIDO');
    for(const [suffix,key,def] of PIANO_SAMPLE_KEYS){
      const id=`pianoSample${i}${suffix}`;
      const val=Number(config[key]??def);
      if(!Number.isFinite(val))continue;
      state[id]=val;
      document.querySelectorAll(`.knob-cell[data-id="${id}"]`).forEach(cell=>{
        if(cell.querySelector('.knob-pointer'))updateSingleKnobCell(cell,val);
      });
    }
  }
}
function updateSampleLayerParameter(id,val){
  const m=/^pianoSample([0-4])(Atk|Dec|Sus|Rel|Hold|Pan|Tune|Cut|Res)$/.exec(id);
  if(!m)return;
  const item=PIANO_SAMPLE_KEYS.find(([suffix])=>suffix===m[2]);
  if(!item||!state.pianoSampleConfig?.[Number(m[1])])return;
  state.pianoSampleConfig[Number(m[1])][item[1]]=Number(val);
}
function resetCurrentSampleEnvelope(){
  const i=editingPianoSampleLayer;
  for(const [suffix,key,def] of PIANO_SAMPLE_KEYS){
    const id=`pianoSample${i}${suffix}`;
    state[id]=def;
    updateSampleLayerParameter(id,def);
    updateAllControlsForId(id,def);
    sendParam(id,def);
  }
}
