// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.8 standalone DSP acceptance tests. No RtAudio, Qt or HTTP needed.
// g++ -std=c++17 -O2 -Isrc -Isrc/core -Isrc/dsp tests/v28_2_8_atmosphere_synth.cpp src/modules/NordSynthModule.cpp -o /tmp/stage_v288test && /tmp/stage_v288test
#include "../src/dsp/NordSlotFxRack.h"
#include "../src/modules/NordSynthModule.h"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
using namespace NordDSP;
struct M { double early=0,late=0,veryLate=0,peak=0; };
static M testFx(int sampleRate,int version,float feedback,float tone,float pitchA,float pitchB,float returnAmt,bool hall=false){
 StudioSlotDSP fx;fx.init(float(sampleRate),6);
 StudioSlotConfig c{};c.type=6;c.enabled=true;c.version=version;
 c.p.fill(.5f);c.p[0]=.95f;c.p[1]=.08f;c.p[2]=returnAmt;
 c.p[3]=.16f;c.p[4]=.88f;c.p[5]=hall?.61f:.91f;c.p[6]=pitchA;c.p[7]=feedback;
 c.p[8]=.1f;c.p[9]=.95f;c.p[10]=.19f;c.p[11]=.25f;
 c.p[12]=.52f;c.p[13]=.71f;c.p[14]=.75f;c.p[15]=.93f;
 c.p[16]=pitchB;c.p[17]=.55f;c.p[18]=.48f;c.p[19]=tone;
 c.p[20]=.9f;c.p[21]=.85f;c.p[22]=.85f;c.p[23]=0.f;
 M m;double accE=0,accL=0,accV=0;int nE=0,nL=0,nV=0;
 const int N=sampleRate*5;
 for(int i=0;i<N;++i){
  if((i&127)==0)fx.syncParameters(c);
  const float in=i<sampleRate/2?(.18f*std::sin(6.283185307f*221.f*i/sampleRate)+.08f*std::sin(6.283185307f*443.f*i/sampleRate)):0.f;
  float l=0,r=0;fx.process(in,in*.92f,c,l,r,120.f);
  assert(std::isfinite(l)&&std::isfinite(r));assert(std::abs(l)<20.f&&std::abs(r)<20.f);
  m.peak=std::max(m.peak,std::max(double(std::abs(l)),double(std::abs(r))));
  const double v=double(l)*l+double(r)*r;
  if(i>=sampleRate && i<sampleRate*2) {accE+=v;nE+=2;}
  if(i>=sampleRate*2 && i<sampleRate*3) {accL+=v;nL+=2;}
  if(i>=sampleRate*4) {accV+=v;nV+=2;}
 }
 m.early=std::sqrt(accE/std::max(1,nE));m.late=std::sqrt(accL/std::max(1,nL));m.veryLate=std::sqrt(accV/std::max(1,nV));return m;
}
static void testSynth(int rate){
 NordSynthModule s;s.init(float(rate));s.synthEnabled=true;s.synthMasterVol=.8f;
 auto bank=s.getWavetableSnapshot();assert(bank&&bank->size()==48); // v31: 36 + 12 LA/vintage tables
 std::vector<float> out(512*2);
 for(int type=21;type<=28;type++){
  for(auto& layer:s.layers){layer.cfg=SynthLayerConfig{};layer.cfg.enabled=false;layer.reset();}
  auto& c=s.layers[0].cfg;c.enabled=true;c.waveType=type;c.volume=.6f;c.sawLevel=1.f;c.pulseLevel=.4f;
  c.cutoffHz=12000.f;c.resonanceQ=1.f;c.attackSec=.005f;c.decaySec=.1f;c.sustainLevel=.8f;
  s.noteOn(72,72,115);double sum=0;
  for(int k=0;k<16;++k){std::fill(out.begin(),out.end(),0.f);s.render(out.data(),512);
   for(auto x:out){assert(std::isfinite(x)&&std::abs(x)<30.f);sum+=double(x)*x;}}
  assert(sum>1e-6 && "Added oscillator not audible");s.allNotesOff();
 }
 // Extreme high key tests FM index bounding (finite, no NaNs).
 for(auto& layer:s.layers){layer.cfg=SynthLayerConfig{};layer.cfg.enabled=false;layer.reset();}
 auto& c=s.layers[0].cfg;c.enabled=true;c.synthesisType=1;c.waveType=10;c.fmIndex=18.f;c.fmRatio=12.f;
 s.noteOn(118,118,127);
 for(int i=0;i<16;++i){std::fill(out.begin(),out.end(),0.f);s.render(out.data(),512);
  for(float x:out)assert(std::isfinite(x)&&std::abs(x)<30.f);}
}
int main(){
 for(int sr:{44100,48000,96000}){
   testSynth(sr);
   const auto shortTail=testFx(sr,4,.04f,.5f,.75f,.64583f,.65f);
   const auto longTail=testFx(sr,4,.985f,.5f,.75f,.64583f,.65f);
   const auto dark=testFx(sr,4,.91f,.02f,.75f,.64583f,.65f);
   const auto bright=testFx(sr,4,.91f,.98f,.75f,.64583f,.65f);
   const auto noPitch=testFx(sr,4,.91f,.5f,.75f,.64583f,0.f);
   const auto hall=testFx(sr,2,.985f,.5f,.75f,.64583f,.65f,true);
   std::cout<<sr<<" Hz: Shimmer short tail "<<shortTail.late<<" long "<<longTail.late
      <<" late(4s) "<<longTail.veryLate<<" dark "<<dark.late<<" bright "<<bright.late
      <<" noPitch "<<noPitch.late<<" hall(4s) "<<hall.veryLate<<" peak "<<longTail.peak<<"\n";
   assert(longTail.veryLate>2e-6);
   assert(longTail.veryLate>hall.veryLate*1.5 && "Shimmer needs a longer audible tail than Hall");
   assert(std::abs(bright.late-dark.late)>1e-6);
   assert(std::abs(noPitch.late-longTail.late)>1e-6);
 }
 std::cout<<"PASS v28.2.8: 8 new oscillator modes, 48 wavetable bank, Shimmer v4 at three sample rates\n";
}
