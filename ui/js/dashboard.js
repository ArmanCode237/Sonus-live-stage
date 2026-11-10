// Indicadores adicionales de la pantalla MAIN. Son solo lectura del estado existente.
// No envían parámetros y no generan peticiones nuevas.
function updateMainDashboard(serverState) {
  if (!$('lcdProgramNumber')) return;
  const activeSlot = Math.max(0, Math.min(7, Number(state.activeProgramSlot) || 0));
  setText('lcdProgramNumber', String(activeSlot + 1).padStart(2,'0'));
  const pianoLayers = state.pianoLayers || [];
  const pianoActive = pianoLayers.filter(l => l?.enabled).length;
  const synthActive = [1,2,3].filter(i => state[`L${i}_en`]).length;
  setText('lcdPianoLayers', `${pianoActive}/5`);
  setText('lcdSynthLayers', `${synthActive}/3`);
  if (serverState && typeof serverState.activeVoices==='number') setText('lcdVoiceCount',serverState.activeVoices);
  const sources=Array.isArray(state.pianoLayerSources)?state.pianoLayerSources:[];
  const layers=Array.isArray(state.pianoLayers)?state.pianoLayers:[];
  const loaded=sources.map((s,i)=>({source:s,layer:layers[i]}))
    .filter(v=>v.layer?.enabled && v.source?.sourcePath);
  const first=loaded[0]?.source?.name || 'NO SAMPLE LOADED';
  const extra=loaded.length>1 ? `  +${loaded.length-1} LAYER${loaded.length>2?'S':''}` : '';
  setText('lcdPianoPreset',first+extra);
  const synthId=Math.max(1,Math.min(48,Number(state.synthPresetId)||1));
  setText('lcdSynthPreset',typeof SYNTH_PRESET_NAMES!=='undefined' ? SYNTH_PRESET_NAMES[synthId] || 'ANALOG PATCH' : 'ANALOG PATCH');
  setText('lcdOrganPreset',typeof ORGAN_MODELS!=='undefined' ? ORGAN_MODELS[state.organModel] || 'B3 TONEWHEEL' : 'B3 TONEWHEEL');
  [['Piano',state.pianoEnabled],['Synth',state.synthEnabled],['Organ',state.organEnabled]].forEach(([name,on])=>{
    const row=$('lcdVoice'+name); if(row)row.hidden=!on;
  });
  const noEngines=$('lcdNoEngines');if(noEngines)noEngines.hidden=!!(state.pianoEnabled||state.synthEnabled||state.organEnabled);
  const modules=[['Piano',state.pianoEnabled],['Synth',state.synthEnabled],['Organ',state.organEnabled]];
  const volumes=[['Piano',state.pianoVolume],['Synth',state.synthMasterVol],['Organ',state.organVolume]];
  for (const [name,volume] of volumes) {
    const v=Math.max(0,Math.min(1.5,Number(volume)||0));
    setText(`main${name}MixValue`,`${Math.round(v*100)}%`);
    const bar=$(`main${name}MixBar`);
    if(bar)bar.style.width=`${Math.min(100,v/1.5*100)}%`;
  }
  for (const [name,enabled] of modules) {
    const mix=$('mainMix'+name);
    if(mix)mix.classList.toggle('is-on',!!enabled);
    const row=$('lcdEngine'+name);
    if(row)row.classList.toggle('is-on',!!enabled);
    setText('lcd'+name+'Status',enabled?'ON':'OFF');
  }
}
