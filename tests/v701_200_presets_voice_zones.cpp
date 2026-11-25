#include "../src/modules/StageSynthModule.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
using namespace SonusDSP;
int main() {
  StageSynthModule synth;synth.init(48000);synth.synthEnabled=true;
  std::vector<float> out(256*2);
  for(int id=1;id<=200;id++) {
    synth.allNotesOff();synth.loadBuiltInSynthPreset(id);
    if(synth.synthPresetId!=id){std::cerr<<"Preset "<<id<<" not loaded\n";return 1;}
    bool ok=false;for(const auto& layer:synth.layers) ok|=layer.cfg.enabled;
    if(!ok){std::cerr<<"No active layer "<<id<<"\n";return 2;}
    synth.noteOn(60,60,100);
    double energy=0;
    for(int block=0;block<36;block++) {
      std::fill(out.begin(),out.end(),0.f);synth.render(out.data(),256);
      for(float s:out){if(!std::isfinite(s)||std::abs(s)>100.f){std::cerr<<"Invalid audio "<<id<<"\n";return 3;}energy+=double(s)*s;}
    }
    if(energy<1e-7){std::cerr<<"Silent preset "<<id<<"\n";return 4;}
  }
  synth.allNotesOff();synth.synthVoiceMaster=false;
  for(auto& l:synth.layers){l.cfg.enabled=true;l.cfg.voiceMode=0;l.cfg.keyLow=0;l.cfg.keyHigh=127;}
  synth.layers[0].cfg.voiceMode=2;
  synth.layers[0].cfg.portamento=.13f;
  synth.layers[1].cfg.voiceMode=0;
  synth.layers[2].cfg.voiceMode=1;
  synth.layers[0].cfg.keyLow=0;synth.layers[0].cfg.keyHigh=59;
  synth.layers[1].cfg.keyLow=60;synth.layers[1].cfg.keyHigh=95;
  synth.layers[2].cfg.keyLow=96;synth.layers[2].cfg.keyHigh=127;
  synth.noteOn(55,55,100);
  if(synth.layers[0].heldKeys.size()!=1||!synth.layers[1].heldKeys.empty()||!synth.layers[2].heldKeys.empty())return 5;
  synth.noteOn(70,70,100);
  bool poly=false;for(auto& v:synth.layers[1].voices)if(v.active&&v.rawKey==70)poly=true;
  if(!poly)return 6;
  synth.noteOn(108,108,100);
  if(synth.layers[2].heldKeys.size()!=1)return 7;
  synth.noteOn(50,50,100);
  if(synth.layers[0].heldKeys.size()!=2)return 8;
  synth.noteOff(50,false);
  bool returned=false;for(auto& v:synth.layers[0].voices)if(v.active&&v.rawKey==55)returned=true;
  if(!returned)return 9;
  synth.allNotesOff();
  // module-wide zone overrides any layer zone
  synth.synthKeyLow=64;synth.synthKeyHigh=80;
  synth.noteOn(60,60,90);
  for(const auto& l:synth.layers)for(const auto& v:l.voices)if(v.active)return 10;
  std::cout<<"PASS: 200 factory patches, individual mono/poly/legato, layer and module keyzones\n";
}
