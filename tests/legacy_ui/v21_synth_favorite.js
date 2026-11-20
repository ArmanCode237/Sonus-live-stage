// SONUS V21: EDIT/DCO favorite resolves currently loaded engine preset and toggles backend.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const src=fs.readFileSync(path.join(__dirname,'../ui/js/presets-v17.js'),'utf8');
let favorites=[],calls=[];
const elements=new Map;
for(const id of ['stageSynthFavorite','stageActive_synth','stageDirty_synth','synthPresetNameMain'])elements.set(id,{id,hidden:false,textContent:'',setAttribute(k,v){this[k]=v},classList:{toggle(){}}});
const w={addEventListener(){},setTimeout,clearTimeout};
const ctx={window:w,console,document:{body:{dataset:{page:'synth'},appendChild(){}},getElementById:id=>elements.get(id)||null,querySelectorAll:()=>[]},sessionStorage:{setItem(){},getItem(){return null}},setTimeout,clearTimeout,fetch:async (url,options)=>{
 calls.push(url);
 const body=options?.body?JSON.parse(options.body):null;
 let result={};
 if(url==='/api/state')result={params:{synthPresetId:73,synthLayers:[],synthLfos:[]},pianoLayers:[]};
 else if(url==='/api/synth/factory/edits')result={favorites:[...favorites],overrides:{},hidden:[]};
 else if(url==='/api/synth/factory/meta'){
  favorites=body.favorite?[...new Set([...favorites,body.id])]:favorites.filter(x=>x!==body.id);
  result={ok:true,metadata:{favorites:[...favorites],overrides:{},hidden:[]}};
 }
 else result={ok:true};
 return {ok:true,json:async()=>result};
},state:{synthPresetId:73},SYNTH_PRESET_NAMES:Array.from({length:385},(_,i)=>`Synth ${i}`),initAllKnobs(){}};
vm.createContext(ctx);vm.runInContext(src,ctx,{filename:'presets-v17.js'});
(async()=>{
 assert.equal(typeof w.stageToggleSynthFavorite,'function');
 await w.stageToggleSynthFavorite();
 assert.deepEqual(favorites,[73]);
 assert.equal(elements.get('stageSynthFavorite').textContent,'★ FAVORITO');
 assert.equal(elements.get('stageSynthFavorite')['aria-pressed'],'true');
 await w.stageToggleSynthFavorite();
 assert.deepEqual(favorites,[]);
 assert.equal(elements.get('stageSynthFavorite').textContent,'☆ FAVORITO');
 assert.equal(calls.filter(x=>x==='/api/synth/factory/meta').length,2);
 console.log('PASS EDIT/DCO favorite: engine fallback, toggle, persistent backend update and UI reflection');
})().catch(e=>{console.error(e);process.exit(1)});
