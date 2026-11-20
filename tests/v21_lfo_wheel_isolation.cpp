// SONUS V21: four independent LFOs and per-layer CC1 mode verification.
// Build: g++ -std=c++17 -O2 -I src/core -I src/dsp tests/v21_lfo_wheel_isolation.cpp src/modules/StageSynthModule.cpp -o /tmp/v21_lfo
#include "../src/modules/StageSynthModule.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace SonusDSP;
using Audio=std::array<std::vector<float>,4>;
static Audio audio(int wheelMode, float cc1, bool lfoLayerD, float cutoffOctaves=0.f){
    StageSynthModule m;
    m.init(48000.f);m.synthEnabled=true;m.synthMasterVol=1.f;
    for(int i=0;i<4;i++){
        auto& cfg=m.layers[i].cfg;
        cfg.enabled=true;cfg.waveType=3;cfg.synthesisType=0;cfg.volume=.6f;
        cfg.cutoffHz=750.f;cfg.resonanceQ=.7f;
        cfg.attackSec=.001f;cfg.decaySec=1.0f;cfg.sustainLevel=.85f;
        cfg.wheelMode=wheelMode;cfg.wheelVibratoCents=65.f;
        cfg.wheelVibratoRateHz=5.5f;cfg.wheelCutoffOctaves=cutoffOctaves;
        cfg.wheelAmpDb=0.f;cfg.wheelWtMorph=0.f;
        for(auto& fx:cfg.insertFx)fx.enabled=false;
        m.layers[i].performanceModWheel=cc1;
        m.lfos[i]=SynthLFO{};
        m.lfos[i].enabled=(i==3)&&lfoLayerD;
        m.lfos[i].rateHz=6.3f;m.lfos[i].depth=.95f;
        m.lfos[i].destPitch=50;m.lfos[i].destCutoff=950;
    }
    m.noteOn(60,60,108);
    Audio stems;for(auto& s:stems)s.assign(24576,0.f);
    std::array<float*,4> ptr{};
    std::vector<float> output(2048);
    for(int off=0;off<12000;off+=1024){
        for(int i=0;i<4;i++)ptr[i]=stems[i].data()+2*off;
        std::fill(output.begin(),output.end(),0.f);
        m.render(output.data(),1024,&ptr);
    }
    for(auto& s:stems)for(float x:s)if(!std::isfinite(x))throw std::runtime_error("Non-finite sample");
    return stems;
}
static double diff(const std::vector<float>& a,const std::vector<float>& b){double s=0;for(size_t i=0;i<a.size();i++)s+=std::abs(double(a[i]-b[i]));return s;}
int main(){
    const auto noWheel=audio(0,0,false),vib=audio(0,1,false);
    for(int i=0;i<4;i++)if(diff(noWheel[i],vib[i])<.01){std::cerr<<"Vibrato not audible on layer "<<i<<"\n";return 1;}
    const auto off=audio(3,1,false);
    for(int i=0;i<4;i++)if(diff(noWheel[i],off[i])>.0001){std::cerr<<"CC1 OFF changed layer "<<i<<"\n";return 2;}
    const auto customZero=audio(1,1,false),customCut=audio(1,1,false,3.f);
    for(int i=0;i<4;i++){
      if(diff(noWheel[i],customZero[i])>.0001){std::cerr<<"Custom mode incorrectly vibrates "<<i<<"\n";return 3;}
      if(diff(noWheel[i],customCut[i])<.005){std::cerr<<"Custom cutoff did not alter layer "<<i<<"\n";return 4;}
    }
    const auto lfoD=audio(3,0,true);
    for(int i=0;i<3;i++)if(diff(off[i],lfoD[i])>.0001){std::cerr<<"LFO D leaked to layer "<<i<<"\n";return 5;}
    if(diff(off[3],lfoD[3])<.01){std::cerr<<"LFO D did not modulate its layer\n";return 6;}
    // Factory change must reset each layer LFO, not inherit previous patch.
    StageSynthModule m;m.init(48000.f);for(auto& l:m.lfos){l.enabled=true;l.rateHz=25.f;l.depth=1.f;l.destPitch=1000;}
    m.loadBuiltInSynthPreset(2);
    for(int i=0;i<4;i++){
      const auto& l=m.lfos[i];
      if(l.rateHz>=20.f || l.destPitch==1000 || l.depth>=1.f){std::cerr<<"LFO leaked across factory presets\n";return 7;}
    }
    std::cout<<"PASS CC1 vibrato/custom/off for four layers; independent LFO D; factory preset reset\n";
}
