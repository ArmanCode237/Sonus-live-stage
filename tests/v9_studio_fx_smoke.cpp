// Live Stage v9: DSP independence, bypass, active tails and bounded stereo effects.
// g++ -std=c++17 -O2 -Isrc/core tests/v9_studio_fx_smoke.cpp -o /tmp/v9_studio_fx_smoke
#include "../src/dsp/StageDspEffects.h"
#include <array>
#include <cmath>
#include <iostream>
using namespace SonusDSP;
static bool finite(float a,float b) { return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a)<6.f&&std::fabs(b)<6.f; }
int main(){
 for(float sr:{44100.f,48000.f,96000.f}){
  StageModuleFxRack rack;rack.init(sr);
  ModuleFxConfig cfg;
  float wetL=99.f,wetR=99.f;
  const float dry[3][2]={{.8f,.25f},{.8f,.25f},{.8f,.25f}};
  rack.process(wetL,wetR,dry,cfg);
  if(wetL!=0.f||wetR!=0.f){std::cerr<<"FX bypass not silent\n";return 1;}
  cfg.flangerEnabled=true;cfg.flangerMix=.8f;cfg.flangerDepth=.7f;cfg.flangerRate=.6f;
  double flangerEnergy=0.0;
  for(int i=0;i<static_cast<int>(sr*.12f);++i){
   const float in[3][2]={{i<1500?.3f:0.f,i<1500?.2f:0.f},{0,0},{0,0}};
   rack.process(wetL,wetR,in,cfg);
   if(!finite(wetL,wetR))return 2;
   flangerEnergy+=double(wetL)*wetL+double(wetR)*wetR;
  }
  if(flangerEnergy<.0001){std::cerr<<"No flanger output\n";return 3;}
  cfg.flangerEnabled=false;cfg.phaserEnabled=true;cfg.phaserMix=.75f;
  for(int i=0;i<2048;++i){rack.process(wetL,wetR,dry,cfg);if(!finite(wetL,wetR))return 4;}
  cfg.phaserEnabled=false;cfg.tremoloEnabled=true;
  for(int i=0;i<2048;++i){rack.process(wetL,wetR,dry,cfg);if(!finite(wetL,wetR))return 5;}
  cfg.tremoloEnabled=false;cfg.delayEnabled=true;cfg.delayMs=40;cfg.delayMix=.8f;
  double tail=0.0;
  for(int i=0;i<static_cast<int>(sr*.09f);++i){
   const float in[3][2]={{0,0},{i==0?.6f:0.f,0.f},{0,0}};
   rack.process(wetL,wetR,in,cfg);if(!finite(wetL,wetR))return 6;
   if(i>static_cast<int>(sr*.03f))tail+=std::fabs(wetL)+std::fabs(wetR);
  }
  if(tail<.001){std::cerr<<"Delay tail missing\n";return 7;}
  cfg.delayEnabled=false;
  rack.process(wetL,wetR,dry,cfg);
  if(wetL!=0.f||wetR!=0.f){std::cerr<<"FX doesn't power down\n";return 8;}
  std::cout<<"PASS "<<sr<<"Hz: independent bypass, flanger/phaser/tremolo, stereo delay tail\n";
 }
}
