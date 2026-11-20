#include "../src/loaders/Sf2Loader.h"
#include "../src/modules/StagePianoEngine.h"
#include <iostream>
#include <cmath>
#include <memory>
#include <vector>
using namespace SonusDSP;
int main(int argc,char**argv){
 if(argc!=3){std::cerr<<"Usage: v19_sf2_real_audio Fantasia.sf2 Roland-D50.sf2\n";return 8;}
 for(int a=1;a<3;a++){
  auto list=Sf2Loader::listPresets(argv[a]);
  if(list.empty()){std::cerr<<"No usable instrument "<<argv[a]<<"\n";return 1;}
  PianoInstrument ins;std::string error;
  if(!Sf2Loader::load(argv[a],ins,error,0)){std::cerr<<"Could not load "<<error;return 2;}
  int regions=0;for(const auto& s:ins.strokes)if(s.lokey<=60&&s.hikey>=60&&s.lovel<=105&&s.hivel>=105)regions++;
  if((a==1&&regions!=1)||(a==2&&regions<4)){std::cerr<<"Wrong region coverage "<<regions;return 3;}
  auto bank=std::make_shared<PianoInstrument>(std::move(ins));
  StagePianoEngine engine;engine.setSampleRate(48000);engine.setPianoEnabled(true);
  engine.multiSynth_.synthEnabled=false;engine.organModule_.organEnabled=false;
  engine.setLayerInstrument(0,bank);
  for(int l=1;l<5;l++)engine.setPianoLayerEnabled(l,false);
  engine.noteOn(60,105);
  std::vector<float> out(1024*2);
  double energy=0;
  for(int block=0;block<36;block++){
   engine.render(out.data(),1024);
   for(float s:out){if(!std::isfinite(s)||std::fabs(s)>8.f){std::cerr<<"Invalid PCM output\n";return 4;}energy+=double(s)*s;}
  }
  if(energy<1e-8){std::cerr<<"No sound for "<<argv[a]<<"\n";return 5;}
  std::cout<<"PASS: "<<bank->name<<", "<<regions<<" overlapping regions at MIDI 60, energy="<<energy<<"\n";
 }
}
