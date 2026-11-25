// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
#include "../src/modules/StageSynthModule.h"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
using namespace SonusDSP;
int main(){
 for(int sr:{44100,48000,96000}){
  StageSynthModule synth; synth.init(float(sr)); synth.synthEnabled=true;
  synth.synthMasterVol=.8f; synth.voiceLimitPerLayer=1;
  for(auto& l:synth.layers){l.cfg=SynthLayerConfig{};l.cfg.enabled=false;l.reset();l.voiceLimit=1;}
  auto& c=synth.layers[0].cfg;c.enabled=true;c.waveType=3;
  c.attackSec=.001f;c.decaySec=.5f;c.sustainLevel=.9f;c.releaseSec=1.5f;
  c.cutoffHz=12000.f;c.resonanceQ=.707f;c.volume=.65f;
  std::vector<float> out(512);
  synth.noteOn(60,60,110);
  float previous=0;
  for(int n=0;n<4;++n){std::fill(out.begin(),out.end(),0);synth.render(out.data(),256);previous=out[out.size()-2];}
  synth.noteOn(74,74,110);std::fill(out.begin(),out.end(),0);synth.render(out.data(),256);
  for(float x:out)assert(std::isfinite(x));
  float first=out[0];float jump=std::abs(first-previous);
  std::cout<<sr<<" Hz steal jump="<<jump<<" (previous="<<previous<<", after="<<first<<")\n";
  assert(jump<.12f);
 }
 std::cout<<"PASS synth voice stealing ramp\n";
}
