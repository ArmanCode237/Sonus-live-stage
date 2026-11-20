'use strict';
const assert=require('assert');
const fs=require('fs');
const vm=require('vm');
const path=require('path');
const root=path.resolve(__dirname,'..');
const nodes={
 soundEditorSaveStatus:{firstChild:{textContent:''}},
 soundEditorDirtyIndicator:{hidden:true},
 modalPianoSample:{},
};
const requests=[];let status='';
const btn={disabled:false};
const ctx={
 state:{pianoLayerSources:[{name:'Trompeta 1',sourcePath:'/samples/trompeta_1.npno'}],pianoSampleConfig:[{}]},
 document:{getElementById:id=>nodes[id]||null,querySelector:selector=>selector.includes('sound-editor-actions')?btn:null},
 window:null,console,
 fetch:async(uri,options)=>{
   requests.push({uri,body:JSON.parse(options.body)});
   return {ok:true,json:async()=>({ok:true,sound:{id:'snd_1',name:'Trompeta 1'}})};
 },
 sendParam(){return Promise.resolve({ok:true});},
 stageProgramMarkDirty(){status='program modified';},
 setText(){},openModal(){},updateSingleKnobCell(){},refreshPianoStudioAux(){}
};
ctx.window=ctx;vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root,'ui/js/piano-sample-editor.js'),'utf8'),ctx);
(async()=>{
  ctx.sampleExpressionSwitch(0,'WheelVibratoOn','wheelVibratoEnabled',true);
  assert.strictEqual(nodes.soundEditorDirtyIndicator.hidden,false);
  assert.strictEqual(status,'program modified');
  await ctx.saveCurrentPianoSound();
  assert.strictEqual(requests.length,1);
  assert.strictEqual(requests[0].uri,'/api/sounds/save');
  assert.strictEqual(requests[0].body.module,'piano');
  assert.strictEqual(requests[0].body.layer,0);
  assert.strictEqual(requests[0].body.bindToSource,true);
  assert.strictEqual(btn.disabled,false);
  assert.strictEqual(nodes.soundEditorDirtyIndicator.hidden,true);
  assert(nodes.soundEditorSaveStatus.firstChild.textContent.includes('guardado'));
  console.log('PASS: Sound saves directly in editor, no navigation or modal, edit indicator');
})().catch(error=>{console.error(error);process.exitCode=1;});
