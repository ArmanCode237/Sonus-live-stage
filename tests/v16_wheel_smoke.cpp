#include "../src/modules/StageSynthModule.h"
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
using namespace SonusDSP;
int main(){
 StageSynthModule s;s.init(48000);s.synthEnabled=true;
 // Family 13 = plucked keys; CC1 changes layer filter and level.
 auto render=[&](float wheel){
  s.loadBuiltInSynthPreset(210);s.synthEnabled=true;
  for(auto& l:s.layers)l.performanceModWheel=wheel;
  std::vector<float> samples(256*2,0);double energy=0;
  s.noteOn(64,64,115);
  for(int i=0;i<12;++i){s.render(samples.data(),256);
   for(float x:samples){if(!std::isfinite(x))return -1.;energy+=double(x)*x;}}
  return energy;
 };
 const double low=render(0),high=render(1);
 std::cout<<"CC1 pluck energies "<<low<<" / "<<high<<"\n";
 if(low<=0||high<=0||std::abs(low-high)<1e-4)return 1;
 s.loadBuiltInSynthPreset(212);s.synthEnabled=true;
 for(auto& l:s.layers)l.performanceModWheel=0;
 const auto factory=s.layers[0].cfg.wheelCutoffOctaves;
 if(!std::isfinite(factory) || s.layers[1].cfg.wheelCutoffOctaves==100)return 2;
 std::cout<<"PASS CC1 destination changes sound without invalid values\n";
}
