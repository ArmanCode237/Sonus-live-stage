#include "../src/dsp/NordDspEffects.h"
#include <iostream>
#include <cmath>
using namespace NordDSP;
int main(){
 for(float sr:{44100.f,48000.f,96000.f}){
  NordModuleFxRack synth, organ;synth.init(sr);organ.init(sr);
  ModuleFxConfig c;c.chorusEnabled=true;c.delayEnabled=true;c.reverbEnabled=true;
  c.driveEnabled=true;c.autoPanEnabled=true;c.reverbType=2;
  double energy=0,tail=0;float peak=0;
  for(int n=0;n<int(sr*0.6f);n++){
   float x[3][2]{};if(n<128){x[0][0]=.12f;x[0][1]=.07f;x[1][0]=.12f;x[1][1]=.07f;x[2][0]=.12f;x[2][1]=.07f;}
   float l,r;synth.process(l,r,x,c);
   if(!std::isfinite(l)||!std::isfinite(r)||std::abs(l)>5||std::abs(r)>5){std::cerr<<"FX unstable: "<<sr<<"\n";return 1;}
   energy+=l*l+r*r;if(n>sr*.3f)tail+=l*l+r*r;
   peak=std::max(peak,std::max(std::abs(l),std::abs(r)));
  }
  if(!(energy>1e-9)){std::cerr<<"FX silent\n";return 2;}
  std::cout<<sr<<" Hz: stable effects, peak "<<peak<<", energy "<<energy<<", tail "<<tail<<"\n";
 }
}
