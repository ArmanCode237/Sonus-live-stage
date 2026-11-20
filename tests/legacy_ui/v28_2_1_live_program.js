'use strict';
const assert=require('assert');
const fs=require('fs');
const vm=require('vm');
const path=require('path');
const nodes={};
const make=(id)=>({id,hidden:false,textContent:'',className:'',
  after(x){nodes[x.id]=x;},setAttribute(){},classList:{toggle(){}}});
nodes.lcdProgramName=make('lcdProgramName');
const state={programs:[{params:{pianoVolume:1,studioRacks:[]},pianoLayers:[]},
                      {params:{pianoVolume:.7,studioRacks:[]},pianoLayers:[]}],activeProgramSlot:0};
let confirms=0,requests=0,fail=false;
const ctx={state,document:{getElementById:id=>nodes[id]||null,createElement:()=>make(''),querySelector:()=>null},
  window:null,console,
  fetch:async(url,opts)=>{
    requests++;
    assert.strictEqual(url,'/api/programs/recall');
    assert.strictEqual(JSON.parse(opts.body).slot,1);
    if(fail)return {ok:false,status:500,json:async()=>({ok:false})};
    return {ok:true,json:async()=>({ok:true,programName:'New Program',params:{pianoVolume:.7},pianoLayers:[]})};
  },
  confirm:()=>{confirms++;throw Error('Live mode must never confirm');},
  $:id=>nodes[id]||null,setText:(id,text)=>{if(nodes[id])nodes[id].textContent=text;},
  updateMainDashboard(){},syncFromServerParams(){},updatePianoLayerSources(){},
  setTimeout(){throw Error('unexpected timer');}
};
ctx.window=ctx;
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/js/programs.js'),'utf8'),ctx);
async function run(){
  ctx.stageProgramMarkDirty();
  assert.strictEqual(nodes.stageProgramDirtyBadge.hidden,false);
  assert.strictEqual(nodes.stageProgramDirtyBadge.textContent,'* MODIFICADO');
  assert.strictEqual(await ctx.onProgramButtonClick(1),true);
  assert.strictEqual(nodes.stageProgramDirtyBadge.hidden,true);
  assert.strictEqual(state.activeProgramSlot,1);
  assert.strictEqual(confirms,0);
  assert.strictEqual(requests,1);
  fail=true;ctx.stageProgramMarkDirty();
  assert.strictEqual(await ctx.onProgramButtonClick(1),false);
  assert.strictEqual(nodes.stageProgramDirtyBadge.hidden,false,'failed recall must retain edit status');
  assert.strictEqual(confirms,0);
  console.log('PASS: non-modal recall, dirty marker before and clean after confirmed transition');
}
run().catch(e=>{console.error(e);process.exitCode=1;});
