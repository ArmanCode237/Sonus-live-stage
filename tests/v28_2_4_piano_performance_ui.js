// No browser or third-party dependencies needed; UI workflow contract.
const vm=require('node:vm');const fs=require('node:fs');const assert=require('node:assert/strict');
const path=require('node:path');
const file=path.join(__dirname,'../ui/js/piano-performances-v2824.js');
const elements={};
function element(id){return elements[id]||(elements[id]={id,value:'',hidden:false,textContent:'',dataset:{},options:[],replaceChildren(option){this.options=[option];this.value='';},add(option){this.options.push(option);}});}
element('pianoPerformanceSelect');element('pianoPerformanceName');element('pianoPerformanceStatus');element('pianoPerformanceDirty');
const calls=[];let globalDirty=0,cleans=0,updates=0;
const params={pianoEnabled:true,pianoLayers:[1,0,0,0,0],studioRacks:[{slots:[],sends:[]},{},{},{}]};
const source=[{sourcePath:'C:/samples/piano.npno',sf2Preset:-1}];
const send=async(url,options)=>{
 calls.push({url,options});
 if(url==='/api/piano/performances')return {ok:true,performances:[{name:'Piano Set',layerCount:5}]};
 if(url==='/api/piano/performances/store')return {ok:true};
 if(url==='/api/state')return {params,pianoLayers:source}; // intentionally lacks "ok"
 if(url==='/api/piano/performances/load')return {ok:true,params,pianoLayers:source};
 throw new Error('Unexpected route '+url);
};
const globals={window:{},document:{readyState:'complete',getElementById:id=>elements[id]||null},Date,console,Option:function(text,value){this.text=text;this.value=value;},fetch:async(url,opts)=>({ok:true,status:200,json:async()=>send(url,opts)}),setTimeout,
 syncFromServerParams:()=>updates++,updatePianoLayerSources:()=>updates++};
globals.window.stageProgramMarkDirty=()=>globalDirty++;
globals.window.stageSoundMarkClean=()=>cleans++;
vm.createContext(globals);
vm.runInContext(fs.readFileSync(file,'utf8'),globals,{filename:file});
(async()=>{
 await globals.window.stagePianoPerformanceRefresh();
 element('pianoPerformanceName').value='Piano Set';
 await globals.window.stagePianoPerformanceStore();
 assert.equal(element('pianoPerformanceStatus').dataset.error,'false');
 assert(calls.some(c=>c.url==='/api/state'));
 assert.equal(element('pianoPerformanceDirty').hidden,true);
 element('pianoPerformanceSelect').value='Piano Set';
 await globals.window.stagePianoPerformanceLoad();
 assert.equal(updates,2);
 assert.equal(cleans,5);
 assert.equal(globalDirty,1); // only Global Program marked dirty, no other module touched
 globals.window.stagePianoPerformanceMarkDirty();
 assert.equal(element('pianoPerformanceDirty').hidden,false);
 globals.window.stagePianoPerformanceOnGlobalRecall();
 assert.equal(element('pianoPerformanceDirty').hidden,true);
 console.log('PASS v28.2.4 UI: store/state without ok, scoped load, live dirty and global recall');
})().catch(e=>{console.error(e);process.exitCode=1;});
