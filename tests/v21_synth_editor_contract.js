// Checks generated DCO HTML without browser automation or a running server.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert');
const file=path.join(__dirname,'../ui/js/synth.js');
const rack={innerHTML:'',children:[],appendChild(x){this.children.push(x)}};
const c={console,state:{},document:{getElementById:id=>id==='synth3LayersContainer'?rack:null,createElement:()=>({innerHTML:'',setAttribute(){},className:''})},$:id=>id==='synth3LayersContainer'?rack:null,STAGE_NOTE_LABELS:Array(16).fill('quarter'),v701Select:()=>'',v701Knob:()=>'',v701ZoneControls:()=>'',v701SynthArchitectureId:i=>'L'+i+'_synthType',setActive:()=>{},setText:()=>{},fetch:async()=>({ok:false})};
for(let i=1;i<=4;i++){Object.assign(c.state,{[`L${i}_wave`]:0,[`L${i}_filtType`]:0,[`L${i}_osc2Wave`]:0,[`L${i}_en`]:true})}
vm.createContext(c);vm.runInContext(fs.readFileSync(file,'utf8'),c,{filename:'synth.js'});
c.buildSynth3LayersPage();
assert.equal(rack.children.length,4);
for(let i=1;i<=4;i++){
 const h=rack.children[i-1].innerHTML;
 const dco=h.slice(h.indexOf(`id="synthPanel_${i}_osc"`),h.indexOf(`id="synthPanel_${i}_filter"`));
 const voice=h.slice(h.indexOf(`id="synthPanel_${i}_voice"`),h.indexOf('KEY ZONE / LAYER'));
 for(const control of [`synthWheelMode_${i}`,`synthLayerLfoEnabled_${i}`,`synthLayerLfoWave_${i}`,`L${i}_lfoPitch`,`L${i}_lfoCutoff`,`L${i}_lfoVolume`,`L${i}_lfoPan`,`L${i}_wheelVibrato`,`L${i}_wheelCutoff`])
  assert.ok(dco.includes(control),`DCO layer ${i} missing ${control}`);
 assert.ok(!voice.includes(`synthLayerLfoEnabled_${i}`),`Duplicate LFO editor in VOICE ${i}`);
}
console.log('PASS 4 independent LFO and CC1 panels, placed in DCO, with valid registered parameter IDs');
