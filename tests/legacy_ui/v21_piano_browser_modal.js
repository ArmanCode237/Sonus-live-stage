// SONUS V21 regression: real modal target selection and per-layer browser state.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
class Element{
 constructor(id=''){this.id=id;this.value='';this.textContent='';this.innerText='';this.style={};this.dataset={};this.children=[];this.options=[];this.innerHTML='';this.listeners={};this.classList={toggle:()=>{},add:()=>{},remove:()=>{}};}
 appendChild(c){this.children.push(c);return c;}
 append(...children){this.children.push(...children);}
 replaceChildren(...children){this.children=children;}
 addEventListener(ev,fn){this.listeners[ev]=fn;}
 click(){this.listeners.click?.();}
 setAttribute(){}querySelector(){return null;}
}
const ids=['modelIndexNum','modelIndexName','quickCatSelect','libraryList','browserCategorySidebar','pianoBrowserLayerTargets','pianoBrowserTargetStatus','pianoLayerHint','searchSampleInput','sortSampleSelect','extFilterSelect',...Array.from({length:5},(_,i)=>`pianoLayerRow${i}`),...Array.from({length:5},(_,i)=>`pianoLayerSelect${i}`),...Array.from({length:5},(_,i)=>`pianoLayerName${i}`),...Array.from({length:5},(_,i)=>`pianoLayerVal${i}`)];
const elements=new Map(ids.map(id=>[id,new Element(id)]));
elements.get('quickCatSelect').options=[{value:'ALL'},{value:'Acoustic Pianos'},{value:'Synth Leads'}];
const samples=[{name:'Acoustic One',path:'C:/A/a.npno',group:'Acoustic Pianos',ext:'.npno',favorite:false,sizeMB:5},{name:'Lead Two',path:'C:/B/b.sf2',group:'Synth Leads',ext:'.sf2',favorite:false,sizeMB:3},{name:'Lead One',path:'C:/B/c.sf2',group:'Synth Leads',ext:'.sf2',favorite:false,sizeMB:3}];
const ctx={console,URL,Map,Set,alert:()=>{},document:{getElementById:id=>elements.get(id)||null,createElement:()=>new Element,querySelectorAll:sel=>sel==='[id^=pianoLayerRow]'?[...elements.values()].filter(x=>x.id.startsWith('pianoLayerRow')):[]},$:id=>elements.get(id)||null,setText:(id,val)=>{if(elements.has(id))elements.get(id).textContent=String(val)},updateAllControlsForId:()=>{},SOUND_GROUPS:['Acoustic Pianos','Synth Leads'],state:{activePianoLayer:0,selectedCategory:'ALL',libraryItems:samples,filteredList:[],pianoLayers:Array.from({length:5},(_,i)=>({enabled:i===0,volume:1})),pianoLayerSources:[{name:'Acoustic One',sourcePath:samples[0].path},{name:'Lead Two',sourcePath:samples[1].path},...Array.from({length:3},()=>({name:'VACÍA',sourcePath:''}))]}};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root,'ui/js/library.js'),'utf8'),ctx,{filename:'library.js'});
vm.runInContext(fs.readFileSync(path.join(root,'ui/js/piano.js'),'utf8'),ctx,{filename:'piano.js'});
ctx.selectPianoLayer(0);
const targets=elements.get('pianoBrowserLayerTargets');
assert.equal(targets.children.length,5);
assert.equal(elements.get('pianoBrowserTargetStatus').textContent.includes('DESTINO · L1'),true);
targets.children[1].click();
assert.equal(ctx.state.activePianoLayer,1);
assert.equal(elements.get('modelIndexNum').textContent,'002');
assert.equal(elements.get('modelIndexName').textContent,'Lead Two');
assert.equal(elements.get('pianoBrowserTargetStatus').textContent.includes('DESTINO · L2'),true);
elements.get('searchSampleInput').value='Lead Two';ctx.renderBrowserList();
targets.children[0].click();assert.equal(elements.get('searchSampleInput').value,'');
targets.children[1].click();assert.equal(elements.get('searchSampleInput').value,'Lead Two');
ctx.state.pianoLayerSources[1]={name:'Lead One',sourcePath:samples[2].path};
ctx.refreshPianoSoundDisplay(true);
assert.equal(elements.get('modelIndexName').textContent,'Lead One');
assert.equal(elements.get('modelIndexNum').textContent,'001');
assert.equal(ctx.state.pianoLayerSources[0].name,'Acoustic One');
assert.ok(fs.readFileSync(path.join(root,'ui/partials/modal-browser.html'),'utf8').includes('id="pianoBrowserLayerTargets"'));
console.log('PASS modal layer targets, independent search, updated selected sound, stable category numbers');
