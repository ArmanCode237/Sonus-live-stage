// Serializes per-layer PCM loads and prevents stale backend results on fast arrows.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('node:assert/strict');
class Node{
 constructor(id=''){this.id=id;this.textContent='';this.innerText='';this.value='';this.children=[];this.options=[];this.style={};this.dataset={};this.classList={toggle(){},add(){},remove(){}};}
 replaceChildren(...xs){this.children=xs;}append(...xs){this.children.push(...xs);}appendChild(x){this.children.push(x)}addEventListener(){}setAttribute(){}
}
const ids=['modelIndexNum','modelIndexName','quickCatSelect','libraryList','browserCategorySidebar','pianoBrowserLayerTargets','pianoBrowserTargetStatus','pianoLayerHint','searchSampleInput','sortSampleSelect','extFilterSelect',...Array.from({length:5},(_,i)=>`pianoLayerRow${i}`),...Array.from({length:5},(_,i)=>`pianoLayerSelect${i}`),...Array.from({length:5},(_,i)=>`pianoLayerName${i}`)];
const els=new Map(ids.map(id=>[id,new Node(id)]));
els.get('quickCatSelect').options=[{value:'ALL'},{value:'Acoustic Pianos'}];
const library=['A','B','C'].map(name=>({name,path:`C:/Samples/${name}.npno`,group:'Acoustic Pianos',ext:'.npno',sizeMB:1,favorite:false}));
const pending=[];
const ctx={Promise,Map,Set,console,alert:()=>{},fetch:(url,opts)=>{
 if(url!=='/api/load')throw Error('Unexpected URL '+url);
 const body=JSON.parse(opts.body);
 return new Promise(resolve=>pending.push({body,resolve}));
},document:{getElementById:id=>els.get(id)||null,createElement:()=>new Node(),querySelectorAll:()=>[]},$:id=>els.get(id)||null,setText:(id,text)=>{if(els.has(id))els.get(id).textContent=String(text);},updateAllControlsForId:()=>{},SOUND_GROUPS:['Acoustic Pianos'],state:{activePianoLayer:0,selectedCategory:'ALL',libraryItems:library,filteredList:[],pianoLayers:Array.from({length:5},()=>({enabled:true,volume:1})),pianoLayerSources:Array.from({length:5},()=>({name:'VACÍA',sourcePath:''}))},syncFromServerParams:()=>{}};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/js/library.js'),'utf8'),ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/js/piano.js'),'utf8'),ctx);
async function flush(){for(let i=0;i<30;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));}
(async()=>{
 const loadA=ctx.loadInstrument(library[0].path,0);
 await flush();assert.equal(pending.length,1);
 const loadB=ctx.loadInstrument(library[1].path,0);
 const loadC=ctx.loadInstrument(library[2].path,0);
 assert.equal(pending.length,1);
 const responseA={ok:true,pianoLayers:[{name:'A',sourcePath:library[0].path}]};
 pending[0].resolve({json:async()=>responseA});
 await flush();assert.equal(pending.length,2);assert.equal(pending[1].body.path,library[2].path);
 const responseC={ok:true,pianoLayers:[{name:'C',sourcePath:library[2].path}]};
 pending[1].resolve({json:async()=>responseC});
 assert.equal(await loadA,false);assert.equal(await loadB,false);assert.equal(await loadC,true);
 assert.equal(els.get('modelIndexNum').textContent,'003');
 console.log('PASS fast L1 changes: only A and C requested, stale A ignored, C shown');
})().catch(err=>{console.error(err);process.exitCode=1;});
