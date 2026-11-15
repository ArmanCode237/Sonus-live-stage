// Regression: g++ -std=c++17 -O2 -pthread -Isrc -Isrc/core -Isrc/modules -Ithird_party tests/v111_synth_factory_and_seq.cpp src/modules/NordSynthModule.cpp -o /tmp/v111_synth_factory_and_seq && /tmp/v111_synth_factory_and_seq
#include "../src/modules/NordSynthModule.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
using namespace NordDSP;
int main(){
 for(int sampleRate:{44100,48000,96000}){
  NordSynthModule synth;synth.init(sampleRate);synth.synthEnabled=true;
  std::vector<float> out(512);
  for(int id=1;id<=256;++id){
   synth.loadBuiltInSynthPreset(id);
   if(synth.synthPresetId!=id){std::cerr<<"id clamped "<<id<<"\n";return 1;}
   bool active=false;for(auto&l:synth.layers)active |= l.cfg.enabled;
   if(!active){std::cerr<<"empty preset "<<id<<"\n";return 2;}
   std::fill(out.begin(),out.end(),0.f);
   synth.render(out.data(),256);
   for(float x:out)if(!std::isfinite(x)||std::abs(x)>1.e-7f){std::cerr<<"ghost "<<id<<"\n";return 3;}
   synth.noteOn(60,60,105);
   double energy=0.;
   for(int k=0;k<25;k++){
    std::fill(out.begin(),out.end(),0.f);
    synth.render(out.data(),256);
    for(float x:out){if(!std::isfinite(x)||std::abs(x)>100.f){std::cerr<<"bad audio "<<id<<"\n";return 4;}energy+=x*x;}
   }
   if(energy<1.e-9){std::cerr<<"silent program "<<id<<"\n";return 5;}
  }
  std::cout<<"PASS "<<sampleRate<<"Hz: 256 playable voices, no stale voices\n";
 }
 NordSynthModule synth;synth.init(48000);synth.synthEnabled=true;
 synth.loadBuiltInSynthPreset(97);
 if(!synth.layers[0].cfg.enabled||!synth.layers[1].cfg.enabled||!synth.layers[2].cfg.enabled||synth.layers[0].cfg.synthesisType!=1||synth.layers[1].cfg.attackSec<.20f){std::cerr<<"97 layered D50 regression\n";return 6;}
 for(int id:{201,207,211,214,219,233,249}){
   synth.loadBuiltInSynthPreset(id);
   for(auto& layer:synth.layers)if(!layer.cfg.enabled){std::cerr<<"fourth layer missing "<<id<<"\n";return 7;}
   if(id==214&&!synth.layers[0].cfg.seqEnabled){std::cerr<<"sequence preset missing\n";return 8;}
   synth.noteOn(60,60,120);
   std::array<std::vector<float>,SYNTH_LAYER_COUNT> buffs;
   for(auto& b:buffs)b.resize(512);
   std::array<float*,SYNTH_LAYER_COUNT> ptrs;
   double energy[4]={0,0,0,0};
   for(int k=0;k<45;++k){
    for(int j=0;j<4;++j){std::fill(buffs[j].begin(),buffs[j].end(),0.f);ptrs[j]=buffs[j].data();}
    std::vector<float> out(512,0.f);
    synth.render(out.data(),256,&ptrs);
    for(int j=0;j<4;++j)for(float x:buffs[j])energy[j]+=x*x;
   }
   for(int j=0;j<4;++j)if(energy[j]<1.e-8){std::cerr<<"silent layer "<<j<<" in "<<id<<"\n";return 9;}
 }
 std::cout<<"PASS D-50-style preset 97, signature four-layer presets, sequencer config\n";
}
