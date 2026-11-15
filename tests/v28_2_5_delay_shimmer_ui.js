// Regression: a slot TAP must never modify master tempo or another delay.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const src=fs.readFileSync('ui/js/studio-rack.js','utf8');
let clock=1000,globalTapCalls=0;
const ctx=vm.createContext({
 console,Math,Number,Map,Set,String,Array,Object,Promise,performance:{now:()=>clock},
 state:{masterBpm:150},
 STAGE_NOTE_BEATS:[4,6,3,2,4/3,1,1.5,.5,.75,.25,.375,.125,.1875,2/3,1/3,1/6],
 STAGE_NOTE_LABELS:['W','W.','H.','H','H3','Q','Q.','E','E.','16','16.','32','32.','Q3','E3','163'],
 stageTapTempo:()=>globalTapCalls++,
 document:{getElementById:()=>({textContent:''}),querySelectorAll:()=>[],addEventListener:()=>{},body:{dataset:{page:'main'}}},
 setTimeout,clearTimeout,fetch:()=>{throw Error('unexpected network call')}
});
vm.runInContext(src,ctx);
vm.runInContext(`
 studioRackReadOnly=false;
 studioRackRender=function(){};
 var calls=[];studioPost=async function(p){calls.push(p);return true;};
 var a=studioRackPreview();studioRackSnapshot=a;
 a.racks[0].slots[0]={type:5,version:3,enabled:true,params:Array(24).fill(.5)};
 a.racks[0].slots[1]={type:5,version:3,enabled:true,params:Array(24).fill(.5)};
 a.racks[0].slots[0].params[6]=0;
 a.racks[0].slots[1].params[6]=0;
 a.racks[0].slots[0].params[8]=(120-30)/270;
 a.racks[0].slots[1].params[8]=(75-30)/270;
 `,ctx);
function tick(t){clock=t;return vm.runInContext('studioDelayTap(0,0)',ctx);}
(async()=>{
 await tick(1000);await tick(1600);
 assert.equal(globalTapCalls,0);
 const vals=vm.runInContext(`({master:state.masterBpm,
    bpmA:studioDelayLocalBpm(studioRackSnapshot.racks[0].slots[0]),
    bpmB:studioDelayLocalBpm(studioRackSnapshot.racks[0].slots[1]),
    calls:calls.slice()})`,ctx);
 assert.equal(vals.master,150);assert(Math.abs(vals.bpmA-100)<.1);
 assert(Math.abs(vals.bpmB-75)<.1);
 assert(vals.calls.some(p=>p.op==='slot'&&p.slot===0&&p.field===10));
 assert(vals.calls.every(p=>p.slot===0));
 await vm.runInContext('studioSlotUpdate(0,0,9,(7.01)/16)',ctx);
 assert.equal(vm.runInContext('studioDelayDivision(studioRackSnapshot.racks[0].slots[0])',ctx),7);
 const markup=vm.runInContext('studioSlotHtml(0,0,studioRackSnapshot.racks[0].slots[0])',ctx);
 assert(markup.includes('TAP DELAY'));assert(markup.includes('BPM DEL DELAY'));
 assert(!markup.includes('onclick="stageTapTempo()"'));
 const sh=vm.runInContext('studioRackSnapshot.racks[0].slots[2]={type:6,version:3,enabled:true,params:Array(24).fill(.5)};studioRackSnapshot.racks[0].slots[2].params[5]=.875;studioSlotHtml(0,2,studioRackSnapshot.racks[0].slots[2])',ctx);
 assert(sh.includes('TONE SHIFT'));assert(sh.includes('PITCH B'));
 assert(sh.includes('studioSlotUpdate(0,2,19,')); // p16 -> field19, 18 is version
 assert.equal(vm.runInContext('studioSlotField(20)',ctx),23);
 console.log('PASS v28.2.5 UI: independent delay TAP, note, local BPM, multi pitch and API fields');
})().catch(e=>{console.error(e);process.exitCode=1});
