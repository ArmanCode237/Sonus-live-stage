#include "../src/modules/StageSynthModule.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>

int main() {
    constexpr int frames = 256;
    constexpr float rates[] = {44100.0f, 48000.0f, 96000.0f};
    for (float sr : rates) {
        SonusDSP::StageSynthModule synth;
        synth.init(sr);
        synth.synthEnabled = true;
        for (int preset=1; preset<=8; ++preset) {
            synth.loadBuiltInSynthPreset(preset);
            assert(synth.layers.size() == 3);
            std::vector<float> buffer(frames * 2, 0.0f);
            for (int midi : {48,52,55,60,64,67,72}) synth.noteOn(midi,midi,112);
            float peak = 0.0f;
            double energy = 0.0;
            for(int block=0; block<100; ++block) {
                std::fill(buffer.begin(), buffer.end(), 0.0f);
                synth.render(buffer.data(),frames);
                for(float sample : buffer) {
                    if (!std::isfinite(sample)) { std::cerr<<"NaN/INF at "<<sr<<", preset "<<preset<<"\n"; return 1; }
                    peak = std::max(peak,std::abs(sample));
                    energy += sample*sample;
                }
            }
            if (energy < 0.01) {std::cerr<<"Silence at "<<sr<<", preset "<<preset<<"\n";return 2;}
            if (peak > 6.0f) {std::cerr<<"Excessive gain "<<peak<<" at "<<sr<<", preset "<<preset<<"\n";return 3;}
            for (int midi : {48,52,55,60,64,67,72}) synth.noteOff(midi,false);
            for(int block=0; block<100; ++block) {
                std::fill(buffer.begin(), buffer.end(), 0.0f);
                synth.render(buffer.data(),frames);
            }
            synth.allNotesOff();
            std::cout<<sr<<" Hz | Factory "<<preset<<" | peak="<<peak<<" | energy="<<energy<<" OK\n";
        }
    }
    // Verify filter modes and PWM can be used across all three independent layers.
    SonusDSP::StageSynthModule extra;
    extra.init(48000.0f);
    extra.synthEnabled=true;
    for(int i=0;i<3;++i) {
        auto& cfg=extra.layers[i].cfg;
        cfg.enabled=true;
        cfg.waveType=9;
        cfg.filterType=i;
        cfg.pwmDepth=0.35f;
        cfg.osc2Level=0.8f;
        cfg.osc2Semitones=i*7;
        cfg.filterEnvAmt=0.6f;
        cfg.cutoffHz=2000.0f;
        cfg.highpassHz=150.0f;
        cfg.drive=0.25f;
    }
    extra.lfos[0].enabled=true;
    extra.lfos[0].depth=0.6f;
    extra.lfos[0].rateHz=4.7f;
    extra.lfos[0].destPitch=25;
    extra.lfos[0].destCutoff=800;
    extra.noteOn(64,64,100);
    float out[512]={};
    extra.render(out,256);
    for(float v:out) assert(std::isfinite(v));
    extra.setSustain(true);
    extra.noteOff(64,true);
    extra.setSustain(false);
    std::cout<<"Analog patch / filters / LFO / sustain: OK\n";
    return 0;
}
