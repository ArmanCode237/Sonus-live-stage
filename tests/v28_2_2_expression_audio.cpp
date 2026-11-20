// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// Regression: expression routes must be opt-in; mono and glide are independent.
// Build with StagePianoEngine.cpp, StageSynthModule.cpp and StageOrganModule.cpp.
#include "../src/modules/StagePianoEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;
static auto bank(){
 auto b=std::make_shared<PianoInstrument>();b->formatType="SF2 (PCM16)";
 StrokeSample s;s.frames=48000*2;s.sampleRate=48000;s.lokey=0;s.hikey=127;
 s.lovel=1;s.hivel=127;s.actualRootMidi=60;s.sampleScale=1.f/32768.f;
 s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
 for(uint32_t i=0;i<s.frames;++i){
  const double x=17500.0*std::sin(i*2.*3.14159265358979*1500./48000.);
  s.pcmL[i]=s.pcmR[i]=int16_t(std::lrint(x));
 }
 s.loop.hasLoop=true;s.loop.start=12000;s.loop.length=36000;
 s.readyFrames->store(s.frames,std::memory_order_release);
 b->strokes.push_back(std::move(s));return b;
}
static void setup(StagePianoEngine& e){
 e.setSampleRate(48000);e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
 e.setInstrument(bank());e.setPianoEnabled(true);
 for(int i=1;i<PIANO_LAYER_COUNT;++i)e.setPianoLayerEnabled(i,false);
}
static std::vector<float> play(float wheel,bool vibrato,bool invertedCutoff=false){
 StagePianoEngine e;setup(e);
 e.setPianoWheelRoute(0,5,vibrato?1.f:0.f);
 e.setPianoWheelRoute(0,6,100.f);
 e.setPianoWheelRoute(0,7,6.f);
 e.setPianoLayerSampleParam(0,7,8000.f);
 if(invertedCutoff){
  e.setPianoWheelRoute(0,0,5.f);
  e.setPianoWheelRoute(0,2,1.f);
  e.setPianoWheelRoute(0,3,1.f);
 }
 e.setModWheel(wheel);
 std::vector<float> out(256*2),samples;samples.reserve(256*45);
 e.noteOn(60,120);
 for(int k=0;k<45;k++){
  e.render(out.data(),256);
  for(int i=0;i<256;++i){assert(std::isfinite(out[2*i]));samples.push_back(out[2*i]);}
 }
 return samples;
}
static double difference(const std::vector<float>& a,const std::vector<float>& b){
 assert(a.size()==b.size());double d=0;
 for(size_t i=2500;i<a.size();++i)d+=std::abs(a[i]-b[i]);
 return d/(a.size()-2500);
}
static double rms(const std::vector<float>& v){
 double x=0;for(size_t i=2500;i<v.size();++i)x+=double(v[i])*v[i];
 return std::sqrt(x/(v.size()-2500));
}
int main(){
 auto off0=play(0,false),off1=play(1,false),vibrato0=play(0,true),vibrato1=play(1,true);
 assert(difference(off0,off1)<1e-6 && "Wheel must do nothing when no route is enabled");
 assert(difference(vibrato0,vibrato1)>.001 && "Vibrato enabled should change pitch with CC1");
 auto inverse0=play(0,false,true),inverse1=play(1,false,true);
 assert(rms(inverse1)>rms(inverse0)*2.0 && "Inverted cutoff CC1=0 must be darker");
 StagePianoEngine e;setup(e);e.setPianoVoiceMode(0,0,1.f);
 std::vector<float> out(256*2);
 e.noteOn(60,105);for(int i=0;i<2;++i)e.render(out.data(),256);
 e.noteOn(64,105);for(int i=0;i<10;++i)e.render(out.data(),256);
 assert(e.getActiveVoiceCount()==1 && "Mono must fade previous voice in same layer");
 e.setPianoVoiceMode(0,0,0.f);e.allNotesOff();
 for(int i=0;i<18;i++)e.render(out.data(),256);
 e.noteOn(60,105);e.render(out.data(),256);
 e.noteOn(64,105);e.render(out.data(),256);
 assert(e.getActiveVoiceCount()>=2 && "Poly must allow independent voices");
 // Same sample zone, MONO+LEGATO retunes the existing playback head rather
 // than retriggering the attack; this must work independently of glide.
 auto nsmp=bank();nsmp->formatType="NSMP SAMPLE (.nsmp4)";
 StagePianoEngine lead;setup(lead);lead.setLayerInstrument(0,nsmp);
 lead.setPianoVoiceMode(0,0,1.f);lead.setPianoVoiceMode(0,1,1.f);
 lead.setSamplePortamento(0,0,0.f);
 lead.noteOn(60,100);for(int n=0;n<4;++n)lead.render(out.data(),256);
 assert(lead.getActiveVoiceCount()==1);
 lead.noteOn(62,100);lead.render(out.data(),256);
 assert(lead.getActiveVoiceCount()==1 && "Legato must reuse same zone phase without a second attack");
 std::cout<<"PASS v28.2.2 expression: opt-in CC1, vibrato, inverted cutoff, mono/poly\n";
}
