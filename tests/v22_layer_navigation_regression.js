// MAIN SOUND NO / arrow navigation across layers, independent of modal search.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
class Element{
 constructor(id=''){this.id=id;this.value='';this.textContent='';this.style={};this.dataset={};this.children=[];this.options=[];this.innerHTML='';this.classList={toggle(){},add(){},remove(){}};this.listeners={};}
 setAttribute(){}appendChild(x){this.children.push(x);}append(...x){this.children.push(...x);}replaceChildren(...x){this.children=x;}
 addEventListener(name,cb){this.listeners[name]=cb;}
}
const names=['modelIndexNum','modelIndexName','quickCatSelect','libraryList','browserCategorySidebar','pianoBrowserLayerTargets','pianoBrowserTargetStatus','pianoLayerHint','searchSampleInput','sortSampleSelect','extFilterSelect',...Array.from({length:5},(_,i)=>`pianoLayerRow${i}`),...Array.from({length:5},(_,i)=>`pianoLayerSelect${i}`),...Array.from({length:5},(_,i)=>`pianoLayerName${i}`),...Array.from({length:5},(_,i)=>`pianoLayerVal${i}`)];
const els=new Map(names.map(id=>[id,new Element(id)]));
els.get('quickCatSelect').options=[{value:'ALL'},{value:'Acoustic Pianos'},{value:'Synth Leads'}];
const library=[
 {name:'Acoustic One',path:'C:/piano/a.npno',group:'Acoustic Pianos',ext:'.npno',favorite:false,sizeMB:4},
 {name:'Lead One',path:'C:/lead/b.sfz',group:'Synth Leads',ext:'.sfz',favorite:false,sizeMB:3},
 {name:'Lead Two',path:'C:/lead/c.sf2',group:'Synth Leads',ext:'.sf2',favorite:false,sizeMB:7},
];
const ctx={console,Map,Set,alert:()=>{},document:{getElementById:id=>els.get(id)||null,createElement:()=>new Element(),querySelectorAll:sel=>sel==='[id^=pianoLayerRow]'?[...els.values()].filter(e=>e.id.startsWith('pianoLayerRow')):[]},$:id=>els.get(id)||null,setText:(id,text)=>{if(els.has(id))els.get(id).textContent=String(text);},updateAllControlsForId:()=>{},SOUND_GROUPS:['Acoustic Pianos','Synth Leads'],state:{activePianoLayer:0,selectedCategory:'ALL',libraryItems:library,filteredList:[],pianoLayers:Array.from({length:5},()=>({enabled:true,volume:1})),pianoLayerSources:[{name:'Acoustic One',sourcePath:library[0].path},{name:'Lead Two',sourcePath:library[2].path},...Array.from({length:3},()=>({name:'VACÍA',sourcePath:''}))]}};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(path.join(root,'ui/js/library.js'),'utf8'),ctx);vm.runInContext(fs.readFileSync(path.join(root,'ui/js/piano.js'),'utf8'),ctx);
const requests=[];
ctx.loadInstrument=(sound,layer)=>{requests.push({sound,layer});ctx.state.pianoLayerSources[layer]={name:library.find(x=>x.path===sound).name,sourcePath:sound};ctx.refreshPianoSoundDisplay(true);return Promise.resolve(true);};
ctx.selectPianoLayer(1);
assert.equal(els.get('modelIndexNum').textContent,'002');
els.get('searchSampleInput').value='does-not-exist';ctx.renderBrowserList();
assert.equal(ctx.state.filteredList.length,0);
ctx.stepLibrary(-1);assert.equal(requests.at(-1).sound,library[1].path);assert.equal(requests.at(-1).layer,1);
assert.equal(els.get('modelIndexNum').textContent,'001');
ctx.selectPianoLayer(0);
assert.equal(els.get('modelIndexName').textContent,'Acoustic One');
// One sound in Acoustic Pianos: navigation must fall back to all sounds.
ctx.stepLibrary(1);assert.equal(requests.at(-1).sound,library[1].path);assert.equal(requests.at(-1).layer,0);
ctx.selectPianoLayer(1);assert.equal(els.get('modelIndexName').textContent,'Lead One');
ctx.selectPianoLayer(0);assert.equal(els.get('modelIndexName').textContent,'Lead One');
const main=fs.readFileSync(path.join(root,'ui/index.html'),'utf8');
assert(main.includes('id="pianoPreviousSound"')&&main.includes('id="pianoNextSound"'));
console.log('PASS: MAIN navigation ignores empty browser filters and keeps selected layers independent');
