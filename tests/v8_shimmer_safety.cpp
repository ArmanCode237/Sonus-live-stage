#include "../src/dsp/StageDspEffects.h"
#include <cmath>
#include <iostream>
using namespace SonusDSP;
int main(){
 for(float sr:{44100.f,48000.f,96000.f}){
  for(int mode=1;mode<=2;mode++){
   ShimmerReverbEngine e;e.init(sr);
   e.setPreDelayMs(18.f);
   e.setParams(.65f,12.f,.72f,.9f,.85f,150.f,7500.f,.42f,.25f,
      mode==1?ShimmerReverbEngine::PitchMode::Single:ShimmerReverbEngine::PitchMode::Dual,
      ShimmerReverbEngine::ReverbMode::BigStereo,ShimmerReverbEngine::ColorMode::Dark);
   double late=0,energy=0;float peak=0;
   const int total=static_cast<int>(sr*1.65f);
   for(int i=0;i<total;i++){
    float l=(i<static_cast<int>(sr*.04f))?.12f*std::sin(2*PI_F*440.f*i/sr):0.f;
    float r=l*.8f;
    e.processStereo(l,r);
    if(!std::isfinite(l)||!std::isfinite(r)){std::cerr<<"Nonfinite shimmer";return 1;}
    peak=std::max(peak,std::max(std::abs(l),std::abs(r)));
    energy+=l*l+r*r;
    if(i>static_cast<int>(sr*.5f))late+=l*l+r*r;
   }
   if(peak>3.f||energy<.01||late<1.e-10){std::cerr<<"Shimmer fail peak="<<peak<<" late="<<late<<"\n";return 2;}
   std::cout<<"PASS shimmer sr="<<sr<<" mode="<<mode<<" peak="<<peak<<" late="<<late<<"\n";
  }
 }
}
