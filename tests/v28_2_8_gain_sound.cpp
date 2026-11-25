// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// v28.2.8 Gain per Sound, PCM16 remains unchanged, no external audio driver.
// Build with StagePianoEngine.cpp, StageSynthModule.cpp and StageOrganModule.cpp.
#include "../src/modules/StagePianoEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;
static std::shared_ptr<PianoInstrument> bank(){
 auto p=std::make_shared<PianoInstrument>();p->formatType="NSMP SAMPLE (.nsmp4)";
 StrokeSample s;s.frames=48000;s.sampleRate=48000;s.actualRootMidi=60;
 s.lokey=0;s.hikey=127;s.lovel=1;s.hivel=127;s.sampleScale=1.f/32768.f;
 s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
 for(uint32_t i=0;i<s.frames;++i){
  const float sample=14000.f*std::sin(6.28318530718f*330.f*i/48000.f);
  s.pcmL[i]=s.pcmR[i]=int16_t(sample);
 }
 s.loop.hasLoop=true;s.loop.start=12000;s.loop.length=24000;
 s.readyFrames->store(s.frames,std::memory_order_release);
 p->strokes.push_back(std::move(s));return p;
}
static double audibleGain(float gain){
 auto p=bank();const int16_t source=p->strokes[0].pcmL[400];
 StagePianoEngine e;e.setSampleRate(48000);e.setInstrument(p);e.setPianoEnabled(true);
 e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
 for(int i=1;i<PIANO_LAYER_COUNT;++i)e.setPianoLayerEnabled(i,false);
 e.setPianoLayerSampleParam(0,9,gain);
 assert(e.getCurrentParams().pianoLayerSample[0].gainDb==gain);
 std::vector<float> out(512*2);e.noteOn(60,112);
 double energy=0;int frames=0;
 for(int b=0;b<80;++b){e.render(out.data(),512);
  if(b>15)for(float x:out){assert(std::isfinite(x));energy+=double(x)*x;frames++;}}
 assert(p->strokes[0].pcmL[400]==source);
 return std::sqrt(energy/frames);
}
int main(){
 // V4 must survive both live edits and full Performance/Program rack restores.
 StagePianoEngine stage;stage.setSampleRate(48000);
 assert(stage.setStudioSlot(0,0,0,6));
 assert(stage.setStudioSlot(0,0,18,4));
 assert(stage.setStudioSlot(0,0,26,1));
 auto rack=stage.getStudioRack(0);
 assert(rack.slots[0].version==4 && rack.slots[0].p[23]==1.f);
 StagePianoEngine restored;restored.setSampleRate(48000);restored.setStudioRack(0,rack);
 auto after=restored.getStudioRack(0);
 assert(after.slots[0].version==4 && after.slots[0].p[23]==1.f);
 const auto normal=audibleGain(0.f), quieter=audibleGain(-12.f), louder=audibleGain(6.f);
 std::cout<<"gain 0dB="<<normal<<" -12dB="<<quieter<<" +6dB="<<louder<<" ratios "<<quieter/normal<<" "<<louder/normal<<"\n";
 assert(normal>0.001);
 assert(quieter<normal*.35&&quieter>normal*.20);
 assert(louder>normal*1.7&&louder<normal*2.2);
 std::cout<<"PASS Sound Gain PCM16 unchanged\n";
}
