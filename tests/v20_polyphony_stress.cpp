/* Regression: 3 running modules and four-layer unison cannot exceed configured max voices. */
#include "../src/modules/StagePianoEngine.h"
#include <cmath>
#include <iostream>
#include <vector>
using namespace SonusDSP;
int main(){
 StagePianoEngine e;e.setSampleRate(48000);e.setMaxVoices(32);
 e.setPianoEnabled(false);
 e.multiSynth_.synthEnabled=true;e.multiSynth_.synthMasterVol=0.65f;
 e.organModule_.organEnabled=true;e.organModule_.organVolume=.50f;
 for(auto& l:e.multiSynth_.layers){l.cfg.enabled=true;l.cfg.volume=.32f;l.cfg.unisonVoices=4;l.cfg.attackSec=.001f;l.cfg.sustainLevel=.8f;l.cfg.releaseSec=2.0f;}
 std::vector<float> audio(256*2,0.f);int peak=0;double energy=0;
 for(int round=0;round<4;round++)for(int n=24;n<=100;n++){
  e.noteOn(n,100);e.render(audio.data(),256);
  const int active=e.getActiveVoiceCount();peak=std::max(peak,active);
  if(active>e.getMaxVoices()){std::cerr<<"Voice overrun "<<active<<" > "<<e.getMaxVoices()<<"\n";return 2;}
  for(float sample:audio){if(!std::isfinite(sample)||std::abs(sample)>12.f){std::cerr<<"Invalid audio output\n";return 3;}energy+=double(sample)*sample;}
 }
 if(peak<=0||energy<=1e-5){std::cerr<<"No audible synth/organ output\n";return 4;}
 std::cout<<"PASS polyphony stress, peak="<<peak<<" / "<<e.getMaxVoices()<<", finite energy="<<energy<<"\n";
}
