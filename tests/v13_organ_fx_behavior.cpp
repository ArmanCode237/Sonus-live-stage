// Build: g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/modules -Isrc/dsp \
//   tests/v13_organ_fx_behavior.cpp src/modules/NordOrganModule.cpp -o /tmp/v13_organ_fx && /tmp/v13_organ_fx
#include "../src/modules/NordOrganModule.h"
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
using namespace NordDSP;
static std::vector<float> render(bool rotaryFast,int scannerMode,bool internalChorus){
    NordOrganModule organ;
    organ.init(48000);organ.organEnabled=true;
    organ.organRotaryFast=rotaryFast;organ.rotaryMix=.83f;
    organ.organDrive=.06f;organ.vibratoMode=scannerMode;
    organ.vibratoDepth=1.2f;organ.vibratoRate=6.4f;
    organ.chorusEnabled=internalChorus;organ.chorusMix=.65f;
    organ.percussionEnabled=false;
    organ.noteOn(60,60,105);
    std::vector<float> out(48000*2/2);
    for(unsigned i=0;i<out.size()/2;i+=128){
        auto size=std::min<unsigned>(128,out.size()/2-i);
        organ.render(out.data()+i*2,size);
    }
    return out;
}
int main(){
    auto normal=render(false,0,false);
    auto fast=render(true,0,false);
    auto scanner=render(false,5,false);
    auto chorused=render(false,0,true);
    auto difference=[&](const std::vector<float>& changed){
        double diff=0,energy=0;
        for(size_t i=0;i<normal.size();i++){
            if(!std::isfinite(changed[i]) || std::fabs(changed[i])>10)return -1.0;
            diff+=std::abs(changed[i]-normal[i]);
            energy+=changed[i]*changed[i];
        }
        return energy>1e-7?diff/normal.size():-1.0;
    };
    const double dRotary=difference(fast),dScanner=difference(scanner),dChorus=difference(chorused);
    if(dRotary<=0.0001 || dScanner<=0.0001 || dChorus<=0.0001){
        std::cerr<<"FAIL rotary="<<dRotary<<" scanner="<<dScanner<<" chorus="<<dChorus<<"\n";
        return 1;
    }
    std::cout<<"PASS ORGAN FX: rotary="<<dRotary<<" scanner="<<dScanner<<" chorus="<<dChorus<<"\n";
}
