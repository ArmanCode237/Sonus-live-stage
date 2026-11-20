// Tests 48 patch IDs with the ACTUAL C++ oscillator engine; no RtAudio/MIDI needed.
#include "../src/modules/StageSynthModule.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
int main() {
    SonusDSP::StageSynthModule synth;
    synth.init(48000.f); synth.synthEnabled = true;
    std::vector<float> audio(512*2);
    int audible=0;
    for (int id=1;id<=48;++id) {
        synth.allNotesOff();
        synth.loadBuiltInSynthPreset(id);
        if (synth.synthPresetId!=id) return 1;
        bool anyEnabled=false;
        for(const auto& layer:synth.layers) anyEnabled |= layer.cfg.enabled && layer.cfg.volume>0.01f;
        if(!anyEnabled) {std::cerr<<"Disabled patch "<<id<<"\n";return 2;}
        synth.noteOn(60,60,120);
        double energy=0;
        for(int block=0;block<120;++block){
            std::fill(audio.begin(),audio.end(),0.f);
            synth.render(audio.data(),512);
            for(float sample:audio) {
                if(!std::isfinite(sample)){std::cerr<<"Nonfinite sample for "<<id<<"\n";return 3;}
                energy+=sample*sample;
            }
        }
        if(energy<1e-6){std::cerr<<"Silent patch "<<id<<"\n";return 4;}
        ++audible;
    }
    std::cout<<"PASS: "<<audible<<" of 48 factory patches render finite, non-silent audio\n";
}
