// g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/modules -Isrc/dsp \
//    tests/v14_rotary_dsp.cpp src/modules/NordOrganModule.cpp -o /tmp/v14_rotary_dsp && /tmp/v14_rotary_dsp
#include "../src/modules/NordOrganModule.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
using namespace NordDSP;
static std::vector<float> output(float speed,float ramp,float width,int sampleRate=48000){
    NordOrganModule organ;
    organ.init(sampleRate);
    organ.organEnabled=true;organ.organRotaryFast=true;
    organ.organVolume=.8f;organ.rotaryMix=1.0f;
    organ.rotaryFastRate=speed;organ.rotaryRamp=ramp;organ.rotaryStereoWidth=width;
    organ.percussionEnabled=false;organ.chorusEnabled=false;organ.vibratoMode=0;
    organ.noteOn(60,60,110);
    std::vector<float> out(sampleRate*2);
    for(unsigned i=0;i<out.size()/2;i+=128){
      const auto frames=std::min<unsigned>(128,out.size()/2-i);
      organ.render(out.data()+i*2,frames);
    }
    return out;
}
static double compare(const std::vector<float>& a,const std::vector<float>& b){
    double diff=0,energy=0;
    for(size_t i=0;i<a.size();++i){
      if(!std::isfinite(a[i])||!std::isfinite(b[i])||std::abs(a[i])>5||std::abs(b[i])>5)return -1;
      diff+=std::abs(a[i]-b[i]);energy+=a[i]*a[i];
    }
    return energy>0.000001?diff/a.size():-1;
}
int main(){
  const auto def=output(1,1,1);
  const double dSpeed=compare(def,output(1.4f,1,1));
  const double dRamp=compare(def,output(1,.45f,1));
  const double dWidth=compare(def,output(1,1,0));
  if(dSpeed<=1e-4||dRamp<=1e-4||dWidth<=1e-4){
    std::cerr<<"FAIL rotary speed="<<dSpeed<<" ramp="<<dRamp<<" width="<<dWidth<<"\n";return 1;
  }
  std::cout<<"PASS ROTARY DSP: FAST SPEED="<<dSpeed<<" RAMP="<<dRamp<<" WIDTH="<<dWidth<<"\n";
}
