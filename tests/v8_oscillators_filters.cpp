#include "../src/modules/StageSynthModule.h"
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <set>
using namespace SonusDSP;
int main(){
 StageSynthModule s;s.init(48000);s.synthEnabled=true;s.synthMasterVol=.9f;
 std::vector<float> out(512*2);
 std::set<int> audible;
 for(int wav=0;wav<15;wav++){
  for(auto& l:s.layers){l.cfg=SynthLayerConfig{};l.cfg.enabled=false;l.reset();}
  auto& c=s.layers[0].cfg;c.enabled=true;c.waveType=wav;c.volume=.8f;
  c.cutoffHz=9500;c.resonanceQ=1.1f;c.osc2Level=.62f;c.osc2Semitones=7;
  c.attackSec=.004f;c.sustainLevel=.65f;c.decaySec=.1f;c.releaseSec=.3f;
  s.noteOn(60,60,112);double power=0;
  for(int k=0;k<25;k++){
   std::fill(out.begin(),out.end(),0.f);s.render(out.data(),512);
   for(float x:out){ if(!std::isfinite(x)||std::abs(x)>20.f)return 2; power+=x*x; }
  }
  if(power < 1.e-6){std::cerr<<"Silent waveform "<<wav<<"\n";return 1;}
  audible.insert(wav);s.allNotesOff();
 }
 for(int mode=0;mode<4;mode++){
  for(auto& l:s.layers){l.cfg=SynthLayerConfig{};l.cfg.enabled=false;l.reset();}
  auto& c=s.layers[0].cfg;c.enabled=true;c.waveType=0;c.filterType=mode;c.cutoffHz=1500;c.resonanceQ=1.5f;
  s.noteOn(60,60,112);double power=0;
  for(int k=0;k<20;k++){std::fill(out.begin(),out.end(),0.f);s.render(out.data(),512);
   for(float x:out){if(!std::isfinite(x)||std::abs(x)>20.f)return 4;power+=x*x;}}
  if(power<1.e-7){std::cerr<<"Silent filter "<<mode<<"\n";return 3;}s.allNotesOff();
 }
 std::cout<<"PASS all "<<audible.size()<<" waveforms and LP HP BP NOTCH filters render stable audio\n";
}
