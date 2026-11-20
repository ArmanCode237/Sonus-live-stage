// Real audio regression for independent per-layer chorus / phaser / delay DSP.
// Compile: g++ -std=c++17 -O2 tests/v15_layer_insert_dsp.cpp -o /tmp/v15_layer_fx
#include "../src/dsp/SynthLayerInsertFx.h"
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
using namespace SonusDSP;

static std::vector<float> impulse(int rate, int unit, float depth=.8f, float lfo=.85f) {
    SynthLayerInsertDSP dsp;
    dsp.init(float(rate));
    std::array<SynthLayerFxParams, 5> effects{};
    effects[size_t(unit)].enabled=true;
    effects[size_t(unit)].depth=depth;
    effects[size_t(unit)].rateHz=lfo;
    effects[size_t(unit)].feedback=.32f;
    effects[size_t(unit)].mix=.72f;
    effects[size_t(unit)].timeMs=90.f;
    const int frames=int(.35*rate);
    std::vector<float> output(size_t(frames)*2);
    for(int i=0;i<frames;i++) {
        float left=(i==0?1.f:0.f),right=(i==0?.7f:0.f);
        dsp.process(left,right,effects);
        if(!std::isfinite(left)||!std::isfinite(right)||std::abs(left)>4||std::abs(right)>4) return {};
        output[size_t(i)*2]=left;output[size_t(i)*2+1]=right;
    }
    return output;
}
int main(){
    for(int rate:{44100,48000,96000}) {
        for(int unit=0;unit<5;unit++) {
            auto samples=impulse(rate,unit);
            if(samples.empty())return 1;
            double diff=0,tail=0;
            for(size_t i=0;i<samples.size();i++){
                const float dry=i==0?1.f:i==1?.7f:0.f;
                diff+=std::pow(samples[i]-dry,2.0);
                if(i>(unit==2 || unit==3 ? 20u : size_t(rate/100)*2))tail+=samples[i]*samples[i];
            }
            if(diff<1e-5||tail<1e-9){std::cerr<<"FX not altering/producing signal: "<<rate<<" "<<unit<<" "<<diff<<" "<<tail<<"\n";return 2;}
        }
        const auto stable=impulse(rate,4,0.f,1.f);
        const auto modulated=impulse(rate,4,.9f,5.f);
        double difference=0;
        for(size_t i=0;i<stable.size();i++)difference+=std::pow(stable[i]-modulated[i],2.0);
        if(difference<1e-5){std::cerr<<"Delay rate/depth do not modulate audio\n";return 3;}
        std::cout<<"PASS "<<rate<<" Hz: five audible effects with appropriate tails, stable DSP, controllable delay modulation\n";
    }
}
