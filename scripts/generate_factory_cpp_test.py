import json
from pathlib import Path
r=Path(__file__).resolve().parents[1];presets=[]
for i in range(1,385):
 p=json.loads((r/f'presets/synth/factory/{i:03d}.json').read_text());ps=p['params'];presets.append(ps)
fields=['enabled','synthesisType','waveType','volume','cutoffHz','resonanceQ','attackSec','decaySec','sustainLevel','releaseSec','filterType','fmIndex','fmRatio','wavetableIdx','wavetableMorph','unisonVoices','osc2Level','wheelWtMorph']
lfields=['enabled','rateHz','depth','destPitch','destCutoff','destVolume','destPan']
code='''// Generated regression fixture from 384 JSON factory sources. Do not edit by hand.
#include "../src/modules/StageSynthModule.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <array>
using namespace SonusDSP;
struct FX{bool on;float mix,rate,depth,feedback,time;bool sync;int division;};
struct V{bool on;int architecture,wave;float vol,cut,res,atk,dec,sus,rel;int filter;float fmIndex,fmRatio;int wt;float morph;int unison;float osc2,wheelWt;FX fx[5];};
struct LFO{bool on;float rate,depth;int pitch,cut;float amp,pan;};
struct Patch{V layers[4];LFO lfos[4];};
static const Patch patches[384]={
'''
for j,ps in enumerate(presets):
 code+=' { { '
 for ly in ps['layers']:
  v=','.join(str(int(ly[k])) if isinstance(ly[k],bool) else str(ly[k]) for k in fields)
  fxs=[]
  for f in ly['insertFx']:
   vals=[f['enabled'],f['mix'],f['rateHz'],f['depth'],f['feedback'],f['timeMs'],f['syncEnabled'],f['syncDivision']]
   fxs.append('{'+','.join(str(int(x)) if isinstance(x,bool) else str(x) for x in vals)+'}')
  code+='{'+v+', {'+','.join(fxs)+'}},'
 code+=' }, { '
 for l in ps['lfos']:
  code+='{'+','.join(str(int(l[k])) if isinstance(l[k],bool) else str(l[k]) for k in lfields)+'},'
 code+=' } },\n'
code+='''};
int main(){
 int counts[5]{},fm=0,wt=0,chorus=0,phaser=0,delay=0,lfo=0;
 for(int sampleRate:{44100,48000,96000}){
  StageSynthModule m;m.init(sampleRate);m.synthEnabled=true;
  std::vector<float> out(512*2);
  for(int i=0;i<384;i++){
   m.loadBuiltInSynthPreset(i+1);
   int enabled=0;
   for(int l=0;l<4;l++){
    auto& v=patches[i].layers[l];auto& c=m.layers[l].cfg;
    c.enabled=v.on;c.synthesisType=v.architecture;c.waveType=v.wave;c.volume=v.vol;c.cutoffHz=v.cut;c.resonanceQ=v.res;
    c.attackSec=v.atk;c.decaySec=v.dec;c.sustainLevel=v.sus;c.releaseSec=v.rel;c.filterType=v.filter;
    c.fmIndex=v.fmIndex;c.fmRatio=v.fmRatio;c.wavetableIdx=v.wt;c.wavetableMorph=v.morph;c.unisonVoices=v.unison;c.osc2Level=v.osc2;c.wheelWtMorph=v.wheelWt;
    enabled+=c.enabled;
    for(int f=0;f<5;f++){
     const auto& source=v.fx[f];auto& target=c.insertFx[f];
     target.enabled=source.on;target.mix=source.mix;target.rateHz=source.rate;target.depth=source.depth;target.feedback=source.feedback;target.timeMs=source.time;target.syncEnabled=source.sync;target.syncDivision=source.division;
     if(c.enabled&&f<2&&source.on)chorus++;
     if(c.enabled&&f==2&&source.on)phaser++;
     if(c.enabled&&f==4&&source.on)delay++;
    }
    const auto& il=patches[i].lfos[l];auto& ol=m.lfos[l];
    ol.enabled=il.on;ol.rateHz=il.rate;ol.depth=il.depth;ol.destPitch=il.pitch;ol.destCutoff=il.cut;ol.destVolume=il.amp;ol.destPan=il.pan;
    lfo+=il.on&&c.enabled;
    if(c.enabled){fm+=c.synthesisType==1;wt+=c.synthesisType==2;}
   }
   if(enabled<1||enabled>4)return 1;
   if(sampleRate==48000)counts[enabled]++;
   std::fill(out.begin(),out.end(),0.f);m.render(out.data(),512);
   double energy0=0;for(float v:out){if(!std::isfinite(v)||std::abs(v)>20)return 2;energy0+=double(v)*v;}
   if(energy0>1e-9){std::cerr<<"Unintended sound on preset load "<<i+1<<' '<<energy0;return 3;}
   m.noteOn(60,60,105);
   double energy=0;
   for(int block=0;block<18;block++){
    std::fill(out.begin(),out.end(),0.f);m.render(out.data(),512);
    for(float v:out){if(!std::isfinite(v)||std::abs(v)>20){std::cerr<<"unstable preset "<<i+1;return 4;}energy+=double(v)*v;}
   }
   if(energy<1e-9){std::cerr<<"silent preset "<<i+1<<' '<<energy;return 5;}
  }
  std::cout<<"PASS "<<sampleRate<<"Hz, 384 loaded audible patches\\n";
 }
 if(*std::min_element(counts+1,counts+5)!=96||fm<100||wt<100||chorus<60||delay<60||lfo<100)return 6;
 std::cout<<"Diversity: 1/2/3/4 layers = "<<counts[1]<<'/'<<counts[2]<<'/'<<counts[3]<<'/'<<counts[4]<<"; FM "<<fm<<" WT "<<wt<<" chorus "<<chorus<<" delay "<<delay<<" LFO "<<lfo<<'\\n';
}
'''
p=r/'tests/v19_factory_audio_validation.cpp';p.write_text(code)
print(p,len(code))
