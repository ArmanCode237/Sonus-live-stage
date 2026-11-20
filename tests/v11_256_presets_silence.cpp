#include "../src/modules/StageSynthModule.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace SonusDSP;
int main(){
 for(int sr:{44100,48000,96000}){
  StageSynthModule synth;synth.init(sr);synth.synthEnabled=true;
  std::vector<float> out(256*2);
  for(int id=1;id<=256;++id){
   synth.loadBuiltInSynthPreset(id);
   if(synth.synthPresetId!=id){std::cerr<<"clamped "<<id<<"\n";return 1;}
   bool enabled=false;for(auto& l:synth.layers)enabled|=l.cfg.enabled;
   if(!enabled){std::cerr<<"empty "<<id<<"\n";return 2;}
   // Changing from a ringing program must not cause voice output with no note-on.
   std::fill(out.begin(),out.end(),0);synth.render(out.data(),256);
   for(float sample:out)if(!std::isfinite(sample)||std::fabs(sample)>1e-7f){std::cerr<<"ghost voice preset="<<id<<" sample="<<sample<<"\n";return 3;}
   synth.noteOn(60,60,105);
   double energy=0.0;
   for(int k=0;k<24;++k){
    std::fill(out.begin(),out.end(),0);synth.render(out.data(),256);
    for(float sample:out){
     if(!std::isfinite(sample)||std::fabs(sample)>100.0f){std::cerr<<"bad sound "<<id<<"\n";return 4;}
     energy+=double(sample)*sample;
    }
   }
   if(energy<1e-9){std::cerr<<"silent "<<id<<"\n";return 5;}
  }
  std::cout<<"PASS "<<sr<<"Hz: 256 playable programs, silence after preset switch\n";
 }
 // The redesigned 097 has independent attack/pad/air layers.
 StageSynthModule synth;synth.init(48000);synth.loadBuiltInSynthPreset(97);
 if(!synth.layers[0].cfg.enabled || !synth.layers[1].cfg.enabled || !synth.layers[2].cfg.enabled ||
     synth.layers[0].cfg.synthesisType!=1 || synth.layers[1].cfg.attackSec<.20f){
   std::cerr<<"D50-style patch 97 structure missing\n";return 6;}
 // Independently reset only synth rack's tail without allocation or init.
 StageModuleFxRack rack;rack.init(48000);
 ModuleFxConfig fx{};fx.delayEnabled=true;fx.delayMix=.8f;fx.delayFeedback=.5f;fx.delayMs=45.f;
 float impulse[3][2]={{0,0},{1,1},{0,0}};float zeros[3][2]={{0,0},{0,0},{0,0}};
 float l=0,r=0;rack.process(l,r,impulse,fx);
 bool tail=false;
 for(int k=0;k<3500;++k){rack.process(l,r,zeros,fx);if(std::fabs(l)+std::fabs(r)>1e-5f)tail=true;}
 if(!tail){std::cerr<<"delay did not generate tail\n";return 7;}
 rack.clearTails();
 for(int k=0;k<4000;++k){rack.process(l,r,zeros,fx);
  if(std::fabs(l)+std::fabs(r)>1e-7f){std::cerr<<"stale fx tail\n";return 8;}}
 std::cout<<"PASS patch097 and local FX tail clearing\n";
}
