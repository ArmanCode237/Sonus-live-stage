// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.4 - deterministic no-hardware DSP smoke/regression tests.
// Compile: g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/dsp tests/v28_2_4_fx_rack.cpp -o fxcheck
#include "../src/dsp/StageSlotFxRack.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace SonusDSP;
static double render(int type,int version,std::array<float,16> params,int sampleRate,int seconds=2){
    StudioSlotDSP fx;fx.init(float(sampleRate),type);
    StudioSlotConfig c{};c.type=type;c.version=version;c.enabled=true;std::copy(params.begin(),params.end(),c.p.begin());
    double rms=0;int count=0;
    for(int i=0;i<sampleRate*seconds;i++){
        if((i&255)==0)fx.syncParameters(c);
        const float input=(i<sampleRate/3) ? .17f*std::sin(6.283185307179586*330.0*i/sampleRate):0.f;
        float l=0,r=0;
        fx.process(input,input*.87f,c,l,r,120.f);
        assert(std::isfinite(l)&&std::isfinite(r));
        assert(std::abs(l)<12.f&&std::abs(r)<12.f);
        if(i>=sampleRate/3){rms+=double(l*l+r*r);count+=2;}
    }
    return std::sqrt(rms/std::max(1,count));
}
static double eqTone(int sampleRate,float freq,float boost){
    StudioSlotDSP eq;eq.init(float(sampleRate),12);
    StudioSlotConfig c{};c.type=12;c.version=2;c.enabled=true;
    c.p.fill(.5f);c.p[0]=1;c.p[1]=0;c.p[2]=1;
    c.p[3]=boost;c.p[4]=.5f;c.p[5]=.5f;
    c.p[6]=.5f;c.p[7]=.5f;c.p[8]=.5f;c.p[9]=.38f;
    c.p[10]=0;c.p[11]=0;c.p[12]=.5f;
    eq.syncParameters(c);
    double s=0;
    for(int i=0;i<sampleRate;i++){
        float x=.2f*std::sin(6.283185307179586*freq*i/sampleRate),l=0,r=0;
        eq.process(x,x,c,l,r);
        if(i>=sampleRate/2) s+=double((x+l)*(x+l));
    }
    return std::sqrt(s/(sampleRate/2));
}
int main(){
 for(int rate:{44100,48000,96000}){
    std::array<float,16> p{};p.fill(.5f);
    for(int type:{1,2,6,7,9,12}){
        p.fill(.5f);p[0]=.65f;p[3]=.65f;
        if(type==6){p[5]=.15f;} // Room model, not shimmer
        if(type==12){p[0]=1;p[1]=0;p[2]=1;p[4]=.8f;p[10]=p[11]=0;}
        auto a=render(type,1,p,rate,1);
        auto b=render(type,2,p,rate,1);
        std::cout<<rate<<"Hz type "<<type<<" legacy="<<a<<" v2="<<b<<"\n";
        assert(std::isfinite(a)&&std::isfinite(b));
    }
    p.fill(.5f);p[0]=.7f;p[5]=.9f;p[7]=.9f;p[12]=.3f;
    double shimmer=render(6,2,p,rate,1);
    assert(std::isfinite(shimmer));
    const float lowFreq=50.f*std::sqrt(10.f); // p6 .5 => sqrt(50*500)
    const double gain=eqTone(rate,lowFreq,.9f),neutral=eqTone(rate,lowFreq,.5f);
    std::cout<<"  EQ boost="<<gain<<" neutral="<<neutral<<" shimmer="<<shimmer<<"\n";
    assert(gain>neutral*1.2);
 }
 std::cout<<"PASS v28.2.4 standalone DSP rack tests\n";
}
