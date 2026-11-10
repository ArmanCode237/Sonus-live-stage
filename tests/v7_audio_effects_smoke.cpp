// Standalone audio effects regression: no RtAudio/RtMidi required.
#include "../src/dsp/NordDspEffects.h"
#include "../src/modules/NordOrganModule.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace NordDSP;
int main(){
 for(float sr: {44100.0f,48000.0f,96000.0f}){
  for(int type=0;type<=2;++type){
   NordReverbEffect rev;rev.init(sr);
   float maxPeak=0,energy=0,late=0;
   const int total=static_cast<int>(sr*2.0);
   for(int i=0;i<total;i++){
    float l=(i==0)?0.5f:0.0f, r=(i==0)?0.13f:0.0f;
    rev.processStereo(l,r,true,type,0.8f,0.7f,0.35f,16.0f,0.0f);
    assert(std::isfinite(l)&&std::isfinite(r));
    maxPeak=std::max(maxPeak,std::max(std::abs(l),std::abs(r)));
    energy+=l*l+r*r;
    if(i>static_cast<int>(sr*.5f))late+=l*l+r*r;
   }
   assert(maxPeak<6.0f&&energy>0.001f&&late>1e-9f);
   std::cout<<"reverb "<<sr<<" type="<<type<<" peak="<<maxPeak<<" late="<<late<<"\n";
  }
  ModulationRack mod;mod.init(sr);float power=0;
  for(int i=0;i<static_cast<int>(sr);++i){
   float L=0.2f*std::sin(2.0f*PI_F*440.0f*i/sr);
   float R=L;
   mod.processStereo(L,R,false,0.0f,0.0f,false,0.0f,0.0f,0.0f,0.0f,
     true,0.7f,0.65f,0.8f,3,0.95f,0.2f,8500.0f,20.0f);
   assert(std::isfinite(L)&&std::isfinite(R));
   power+=L*L+R*R;
  }
  assert(power>5.0f);
  NordOrganModule organ;organ.init(sr);organ.organEnabled=true;
  organ.chorusEnabled=true;organ.chorusMix=0.4f;organ.chorusWidth=1.0f;
  organ.noteOn(60,60,100);
  std::vector<float> out(512*2);
  double organEnergy=0;
  for(int j=0;j<static_cast<int>(sr/512.0f);++j){
   std::fill(out.begin(),out.end(),0.0f);
   organ.render(out.data(),512);
   for(float sample:out){assert(std::isfinite(sample));assert(std::abs(sample)<10.0f);organEnergy+=sample*sample;}
  }
  assert(organEnergy>0.1);
  std::cout<<"organ "<<sr<<" energy="<<organEnergy<<"\n";
 }
 std::cout<<"PASS v7 DSP effects and organ chorus\n";
}
