// v26: Non-destructive offline regression of all Shimmer knobs and tails.
// Run without an audio device: g++ -std=c++17 -O2 -Isrc/core ...
#include "../src/dsp/StageSlotFxRack.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace SonusDSP;

static StudioSlotConfig setup(){
    StudioSlotConfig c;
    c.type=6;c.enabled=true;
    c.p={{.87f,.42f,.78f,.18f,.7f,.99f,
          .9f,.75f,.24f,.72f,.39f,.63f,.30f,.85f,.15f,.74f}};
    return c;
}
static std::vector<float> render(const StudioSlotConfig& cfg,int rate=16000,int frames=32000){
    StudioSlotDSP dsp;dsp.init(float(rate),6);
    std::vector<float> out(size_t(frames)*2);
    for(int i=0;i<frames;++i){
        float l=0.f,r=0.f;
        if(i<rate/5){
            const float t=float(i)/rate;
            l=0.085f*(std::sin(2*PI_F*230*t)+0.7f*std::sin(2*PI_F*810*t)+
                .5f*std::sin(2*PI_F*2450*t));
            r=.6f*l+.012f*std::sin(2*PI_F*1170*t);
        }
        dsp.process(l,r,cfg,out[size_t(i)*2],out[size_t(i)*2+1]);
        if(!std::isfinite(out[size_t(i)*2])||!std::isfinite(out[size_t(i)*2+1]) ||
            std::abs(out[size_t(i)*2])>8 || std::abs(out[size_t(i)*2+1])>8)
            throw std::runtime_error("nonfinite / unstable shimmer");
    }
    return out;
}
static double difference(const std::vector<float>& a,const std::vector<float>& b){
    double energy=0;
    for(size_t i=16000;i<a.size();++i){const double d=double(a[i])-double(b[i]);energy+=d*d;}
    return energy;
}
int main(){
    try {
        auto base=setup(); const auto ref=render(base);
        constexpr int controls[]={0,1,2,3,4,6,7,8,9,10,11,12,13,14,15};
        for(int p:controls){
            auto other=base;
            other.p[p]=(p==13)?0.03f:((p==12||p==14)?1.f:(base.p[p]<.5f?.99f:.03f));
            const double delta=difference(ref,render(other));
            std::cout<<"Knob p"<<p<<" delta="<<delta<<"\n";
            if(delta<1e-9){std::cerr<<"Inert shimmer knob p"<<p<<"\n";return 2;}
        }
        // Feedback must change the long tail's magnitude, not merely the UI.
        auto low=base,high=base; low.p[7]=0.f;high.p[7]=1.f;
        const auto a=render(low,16000,16000*5), b=render(high,16000,16000*5);
        double lateLow=0,lateHigh=0;
        for(size_t i=16000*3*2;i<a.size();++i){lateLow+=double(a[i])*a[i];lateHigh+=double(b[i])*b[i];}
        std::cout<<"3-5sec late energy feedback=0 "<<lateLow<<" feedback=100 "<<lateHigh<<"\n";
        if(!(lateHigh>lateLow*3 && lateHigh>1e-10)){std::cerr<<"Feedback does not sustain\n";return 3;}
        std::cout<<"PASS v26 shimmer controls and long tails\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 4;}
}
