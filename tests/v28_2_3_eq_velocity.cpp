// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.3: mono-legato velocity updates, parametric Sound EQ, 12/24/48 slopes.
// g++ -std=c++17 -O2 -pthread -I src ... [engine + synth + organ]
#include "../src/modules/StagePianoEngine.h"
#include <array>
#include <cmath>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;
constexpr double pi=3.14159265358979323846;
static double tone(float hz,int slope,bool highPass){
  MasterEqualizer eq;EqBandParams b[5]{};
  for(auto& x:b){x.freqHz=1000;x.q=1;x.gainDb=0;}
  eq.update(48000.f,true,highPass?500.f:20.f,highPass?20000.f:4000.f,b,
            highPass?slope:12,highPass?12:slope);
  double sum=0;const int total=48000;
  for(int i=0;i<total;++i){float x=.1f*std::sin(2*pi*hz*i/48000.0),l=x,r=x;
    eq.processStereo(l,r);assert(std::isfinite(l)&&std::isfinite(r));
    if(i>24000)sum+=double(l)*l;
  }
  return std::sqrt(sum/23999.0);
}
static std::shared_ptr<PianoInstrument> bank(){
 auto b=std::make_shared<PianoInstrument>();b->formatType="NSMP SAMPLE (.nsmp4)";
 StrokeSample s;s.frames=48000*3;s.sampleRate=48000;s.lokey=0;s.hikey=127;s.lovel=1;s.hivel=127;s.actualRootMidi=60;
 s.sampleScale=1.f/32768.f;
 s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
 for(uint32_t i=0;i<s.frames;++i){auto v=int16_t(std::lrint(13000.*std::sin(2*pi*440*i/48000.0)));s.pcmL[i]=s.pcmR[i]=v;}
 s.loop.hasLoop=true;s.loop.start=12000;s.loop.length=48000;s.readyFrames->store(s.frames);
 b->strokes.push_back(std::move(s));return b;
}
static double measure(StagePianoEngine& e,int blocks){
 std::array<float,512> out{};double sum=0;int n=0;
 for(int j=0;j<blocks;++j){e.render(out.data(),256);
   for(float x:out){assert(std::isfinite(x));sum+=double(x)*x;n++;}}
 return std::sqrt(sum/n);
}
int main(){
 const double p12=tone(100,12,true),p24=tone(100,24,true),p48=tone(100,48,true);
 std::cout<<"HPF 100 Hz RMS "<<p12<<" "<<p24<<" "<<p48<<"\n";
 assert(p12>p24*1.6 && p24>p48*1.5);
 const double l12=tone(10000,12,false),l24=tone(10000,24,false),l48=tone(10000,48,false);
 assert(l12>l24*1.2&&l24>l48*1.2);
 for(int rate:{44100,48000,96000}){
  StagePianoEngine e;e.setSampleRate(rate);e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
  for(int i=1;i<PIANO_LAYER_COUNT;++i)e.setPianoLayerEnabled(i,false);
  e.setLayerInstrument(0,bank());e.setPianoEnabled(true);
  e.setPianoVoiceMode(0,0,1);e.setPianoVoiceMode(0,1,1);
  e.noteOn(60,35);measure(e,22);
  const double quiet=measure(e,6);
  e.noteOn(62,124);measure(e,10);const double loud=measure(e,8);
  assert(e.getActiveVoiceCount()==1);
  std::cout<<rate<<"Hz quiet="<<quiet<<" loud="<<loud<<"\n";
  assert(loud>quiet*1.8 && "Mono legato velocity must update smoothly with a new note");
  // Sound-local EQ is independent per layer and saved in preset snapshots.
  e.setSoundEqParameter(0,0,1);e.setSoundEqParameter(0,5,440);e.setSoundEqParameter(0,6,.7);e.setSoundEqParameter(0,7,-12);
  e.setSoundEqParameter(0,3,48);e.setSoundEqParameter(0,1,200);
  auto params=e.getCurrentParams();assert(params.pianoLayerSample[0].soundEqEnabled);
  assert(params.pianoLayerSample[0].soundEqGain[0]<-11.9f);
  assert(!params.pianoLayerSample[1].soundEqEnabled);
  e.applyPreset(params);measure(e,5);
 }
 std::cout<<"PASS v28.2.3 Sound EQ / rack cut slopes / mono velocity\n";
}
