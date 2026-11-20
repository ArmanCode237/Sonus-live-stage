#include "../src/modules/StageSynthModule.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
#include <set>
using namespace SonusDSP;
int main() {
    StageSynthModule synth;
    synth.init(48000); synth.synthEnabled=true;
    std::vector<float> buffer(256*2);
    int active=0;
    std::set<int> waveTypes;
    for (int id=1;id<=120;id++) {
        synth.allNotesOff();synth.loadBuiltInSynthPreset(id);
        if(synth.synthPresetId!=id){std::cerr<<"Incorrect id "<<id<<" got "<<synth.synthPresetId<<"\n";return 1;}
        bool hasEnabled=false;
        for(auto& l:synth.layers)if(l.cfg.enabled){hasEnabled=true;waveTypes.insert(l.cfg.waveType);}
        if(!hasEnabled){std::cerr<<"No enabled layer for patch "<<id<<"\n";return 2;}
        synth.noteOn(60,60,120);
        double energy=0;
        for(int k=0;k<80;k++){
            std::fill(buffer.begin(),buffer.end(),0.f);
            synth.render(buffer.data(),256);
            for(float v:buffer){ if(!std::isfinite(v) || std::abs(v)>100.f){std::cerr<<"Unstable patch "<<id<<"\n";return 3;}energy+=v*v; }
        }
        if(energy<1.e-6){std::cerr<<"Silent patch "<<id<<"\n";return 4;}
        active++;
    }
    // Mono legato keeps existing voices/envelopes, retargets their pitch.
    synth.loadBuiltInSynthPreset(25);synth.synthVoiceMode=2;synth.synthPortamento=.18f;
    synth.synthGlideMode=1;synth.synthSlide=-5.f;
    synth.noteOn(60,60,100);
    std::fill(buffer.begin(),buffer.end(),0.f);synth.render(buffer.data(),256);
    synth.noteOn(72,72,100);
    if(!synth.layers[0].voices[0].active){std::cerr<<"Legato lost voice\n";return 5;}
    bool foundRetarget=false;
    for(const auto& v:synth.layers[0].voices) if(v.active && v.rawKey==72 && v.targetFreq>v.freq){foundRetarget=true;}
    if(!foundRetarget){std::cerr<<"Portamento target not updated\n";return 6;}
    for(int k=0;k<100;k++){std::fill(buffer.begin(),buffer.end(),0.f);synth.render(buffer.data(),256);}
    synth.noteOff(72,false);
    bool returned=false;
    for(const auto& v:synth.layers[0].voices)if(v.active&&v.rawKey==60)returned=true;
    if(!returned){std::cerr<<"Monophonic key priority regression\n";return 7;}
    synth.noteOff(60,false);
    synth.allNotesOff();
    std::cout<<"PASS "<<active<<" patches, "<<waveTypes.size()<<" oscillator families, mono legato and portamento\n";
}
