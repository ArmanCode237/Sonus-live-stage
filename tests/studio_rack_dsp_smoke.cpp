// Run: g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/modules -Ithird_party tests/studio_rack_dsp_smoke.cpp src/modules/{StagePianoEngine,StageSynthModule,StageOrganModule}.cpp -o /tmp/studio_rack_smoke && /tmp/studio_rack_smoke
#include "../src/modules/StagePianoEngine.h"
#include <vector>
#include <cmath>
#include <iostream>
using namespace SonusDSP;
int main(){
  for(float sr:{44100.f,48000.f,96000.f}){
    StagePianoEngine e;
    e.setSampleRate(sr);
    // Default new program; support two distinct instances of the same effect type.
    e.setStudioRoutingEnabled(true);
    if(!e.setStudioSlot(1,0,0,6)||!e.setStudioSlot(1,0,1,1))return 1;
    if(!e.setStudioSlot(1,1,0,6)||!e.setStudioSlot(1,1,1,1))return 2;
    e.setStudioSlot(1,1,7,.76f); // Different reverb model, different state.
    e.setStudioSlot(1,2,0,5);e.setStudioSlot(1,2,1,1);
    e.setStudioSlot(1,3,0,9);e.setStudioSlot(1,3,1,1);
    e.setStudioSlot(1,4,0,11);e.setStudioSlot(1,4,1,1);
    for(int slot=0;slot<5;slot++)if(!e.setStudioSend(1,0,slot,.28f))return 3;
    auto cfg=e.getStudioRack(1);
    if(!e.swapStudioSlots(1,0,2))return 11;
    const auto moved=e.getStudioRack(1);
    if(moved.slots[0].type!=5||moved.slots[2].type!=6||moved.sends[0][0]!=.28f)return 12;
    e.swapStudioSlots(1,0,2);
    if(cfg.slots[0].type!=6||cfg.slots[1].type!=6||cfg.slots[2].type!=5||cfg.slots[3].type!=9)return 4;
    if(!e.setStudioEq(1,1,3.0f)||!e.setStudioEqEnabled(1,true))return 5;
    // V11.1: verify every movable-EQ parameter round trips to the DSP state.
    if(!e.setStudioEqParam(1,0,78.f)||!e.setStudioEqParam(1,1,16000.f)||
       !e.setStudioEqParam(1,2,-3.f)||!e.setStudioEqParam(1,3,1.5f)||
       !e.setStudioEqParam(1,4,4.f)||!e.setStudioEqParam(1,5,120.f)||
       !e.setStudioEqParam(1,10,2.1f))return 13;
    auto eq=e.getStudioRack(1);
    if(eq.eqPosition!=4||eq.eqHighPass!=78.f||eq.eqLowPass!=16000.f||
       eq.eqInputDb!=-3.f||eq.eqOutputDb!=1.5f||eq.eqFreq[0]!=120.f||
       std::fabs(eq.eqQ[0]-2.1f)>1.e-5f)return 14;
    // Slopes 12/24/48 dB/oct were introduced in v28.2.3; field 15 is valid.
    if(!e.setStudioEqParam(1,15,24.f)||e.setStudioEqParam(1,4,-INFINITY))return 15;

    if(e.setStudioSlot(0,5,0,11)||e.setStudioSend(3,4,0,1))return 6;
    auto p=e.getCurrentParams();
    p.pianoEnabled=false;p.synthEnabled=true;p.organEnabled=false;
    p.synthLayers[0].enabled=true;
    e.applyPreset(p);
    e.noteOn(60,105);
    std::vector<float> samples(256*2);
    double energy=0;
    for(int block=0;block<int(sr/256*.25);block++){
        e.render(samples.data(),256);
        for(float x:samples){if(!std::isfinite(x)||std::abs(x)>1.05f){std::cerr<<"invalid DSP sample "<<sr<<' '<<x<<'\n';return 7;}energy+=double(x)*x;}
    }
    if(energy<1e-5){std::cerr<<"silent synth rack\n";return 8;}
    e.setStudioSlot(1,0,1,0); // bypass should not free DSP on audio thread
    e.setStudioSlot(1,0,0,10);e.setStudioSlot(1,0,1,1);
    for(int block=0;block<96;block++){
      e.render(samples.data(),256);
      for(float x:samples)if(!std::isfinite(x)||std::abs(x)>1.05f)return 9;
    }
    e.setStudioRoutingEnabled(false);
    e.render(samples.data(),256);
    for(float x:samples)if(!std::isfinite(x))return 10;
    std::cout<<"PASS "<<sr<<" Hz, two independent reverbs, five slots, EQ, sends, legacy bypass; energy="<<energy<<'\n';
  }
}
