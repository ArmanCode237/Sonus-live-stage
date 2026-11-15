// Browser state regression in a minimal DOM: no Chromium or live backend required.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.resolve(__dirname,'..');
class Element{
 constructor(id=''){this.id=id;this.value='';this.textContent='';this.innerText='';this.style={};this.dataset={};this.children=[];this.classList={toggle:()=>{},add:()=>{},remove:()=>{}};this.options=[];this.innerHTML='';}
 appendChild(c){this.children.push(c)}append(...children){this.children.push(...children)}
 setAttribute(){}querySelector(){return null}
}
const elements=new Map;
for(const id of ['modelIndexNum','modelIndexName','quickCatSelect','libraryList','browserCategorySidebar','pianoLayerHint','searchSampleInput','sortSampleSelect','extFilterSelect',...Array.from({length:5},(_,i)=>`pianoLayerRow${i}`),...Array.from({length:5},(_,i)=>`pianoLayerSelect${i}`),...Array.from({length:5},(_,i)=>`pianoLayerName${i}`),...Array.from({length:5},(_,i)=>`pianoLayerVal${i}`)])elements.set(id,new Element(id));
elements.get('quickCatSelect').options=[{value:'ALL'},{value:'Acoustic Pianos'},{value:'Synth Leads'}];
const library=[{name:'Acoustic One',path:'C:/Samples/a.npno',group:'Acoustic Pianos',ext:'.npno',favorite:false,sizeMB:5},{name:'Lead Two',path:'C:/Samples/b.sf2',group:'Synth Leads',ext:'.sf2',favorite:false,sizeMB:3},{name:'Lead One',path:'C:/Samples/c.sf2',group:'Synth Leads',ext:'.sf2',favorite:false,sizeMB:3}];
const ctx={console,URL,Map,Set,alert:()=>{},document:{getElementById:id=>elements.get(id)||null,createElement:()=>new Element,querySelectorAll:selector=>selector==='[id^=pianoLayerRow]'?[...elements.values()].filter(x=>x.id.startsWith('pianoLayerRow')):[]},$:id=>elements.get(id)||null,setText:(id,val)=>{if(elements.has(id))elements.get(id).textContent=String(val)},updateAllControlsForId:()=>{},SOUND_GROUPS:['Acoustic Pianos','Synth Leads'],state:{activePianoLayer:0,selectedCategory:'ALL',libraryItems:library,filteredList:[],pianoLayers:Array.from({length:5},(_,i)=>({enabled:i===0,volume:1})),pianoLayerSources:[{name:'Acoustic One',sourcePath:'C:/Samples/a.npno'},{name:'Lead Two',sourcePath:'C:/Samples/b.sf2'},{name:'VACÍA',sourcePath:''},{name:'VACÍA',sourcePath:''},{name:'VACÍA',sourcePath:''}]}};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root,'ui/js/library.js'),'utf8'),ctx,{filename:'library.js'});
vm.runInContext(fs.readFileSync(path.join(root,'ui/js/piano.js'),'utf8'),ctx,{filename:'piano.js'});
ctx.selectPianoLayer(0);
assert.equal(elements.get('modelIndexNum').textContent,'001');assert.equal(elements.get('modelIndexName').textContent,'Acoustic One');
ctx.selectPianoLayer(1);
assert.equal(elements.get('modelIndexNum').textContent,'002');assert.equal(elements.get('modelIndexName').textContent,'Lead Two');
assert.equal(ctx.state.selectedCategory,'Synth Leads');
elements.get('searchSampleInput').value='Lead Two';ctx.renderBrowserList();
ctx.selectPianoLayer(0);
assert.equal(elements.get('modelIndexNum').textContent,'001');assert.equal(elements.get('searchSampleInput').value,'');
ctx.selectPianoLayer(1);
assert.equal(elements.get('searchSampleInput').value,'Lead Two');
assert.equal(ctx.state.pianoLayerSources[0].sourcePath,'C:/Samples/a.npno');
assert.equal(ctx.state.pianoLayerSources[1].sourcePath,'C:/Samples/b.sf2');
console.log('PASS piano layer selection, independent search, category and stable sound number');
