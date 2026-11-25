// Regression: sample-loop seam, finite audio under retriggering, short polyphony
// transitions, and reclamation of decoded instruments off the audio callback.
#include "../src/modules/StagePianoEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;
static std::shared_ptr<PianoInstrument> makeBank(bool looping){
 auto b=std::make_shared<PianoInstrument>();b->name="Synthetic sample";b->formatType="SF2";
 StrokeSample s;
 s.frames=24000;s.sampleRate=48000;s.actualRootMidi=60;
 s.lokey=0;s.hikey=127;s.lovel=0;s.hivel=127;s.levelGainLinear=1.f;
 s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
 s.loop.hasLoop=looping;s.loop.start=4000;s.loop.length=8000;s.sf2LoopMode=1;
 for(unsigned i=0;i<s.frames;i++){
   // Seamless authored factory loop: 8000 frames = exactly 40 periods at 240 Hz.
   // The previous ramp fixture had a discontinuity of 2.0 at the seam, which
   // incorrectly required an always-on crossfade to make the test pass.
   const float value=(i<4000)?0.f:((i<12000)?0.7f*std::sin(2.*3.141592653589793*240.*(i-4000)/48000.):0.f);
   s.pcmL[i]=s.pcmR[i]=static_cast<int16_t>(std::lrint(value*32767.f));
 }
 s.sampleScale=1.f/32768.f;s.readyFrames->store(s.frames);b->strokes.push_back(std::move(s));return b;
}
int main(int argc,char** argv){
 const double outputRate=argc>1?std::atof(argv[1]):48000.0;
 if(outputRate<22050 || outputRate>192000)return 20;
 StagePianoEngine e;e.setSampleRate(outputRate);
 e.setPianoEnabled(true);e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
 for(int i=1;i<5;i++)e.setPianoLayerEnabled(i,false);
 e.setPianoLayerSampleParam(0,0,.005f);e.setPianoLayerSampleParam(0,2,1.f);
 // Bypass the factory reverb to observe loop discontinuities directly.
 e.setStudioSlot(3,0,0,0);
 auto b=makeBank(true);std::weak_ptr<const PianoInstrument> weak=b;
 e.setLayerInstrument(0,b);b.reset();
 e.noteOn(60,127);
 std::vector<float> buffer(256*2);
 float maxAdjacent=0.f;float prev=0.f;bool havePrev=false;double energy=0;
 for(int block=0;block<300;block++){
    e.render(buffer.data(),256);
    for(unsigned i=0;i<256;i++){
       const float L=buffer[i*2],R=buffer[i*2+1];
       if(!std::isfinite(L)||!std::isfinite(R))return 10;
       energy+=double(L)*L;
       if(block>80 && havePrev)maxAdjacent=std::max(maxAdjacent,std::abs(L-prev));
       prev=L;havePrev=true;
    }
 }
 if(energy<1e-4 || maxAdjacent>0.17f){
   std::cerr<<"Loop seam failed: energy="<<energy<<" max jump="<<maxAdjacent<<"\n";return 11;
 }
 e.allNotesOff();e.setLayerInstrument(0,{});
 // v32: all-notes-off fades voices out in 18 ms instead of cutting them: render
 // long enough for the fade at 96 kHz too (12 x 256 frames >= 32 ms).
 for(int i=0;i<12;i++)e.render(buffer.data(),256);
 e.collectRetiredInstruments();
 if(!weak.expired()){std::cerr<<"Retired bank still held by inactive voices\n";return 12;}
 // Repeated note lifecycle, forced reduction of polyphony and bank replacements.
 for(int round=0;round<120;round++){
   auto bank=makeBank(false);
   std::weak_ptr<const PianoInstrument> ref=bank;
   e.setLayerInstrument(0,bank);bank.reset();
   e.setMaxVoices(round%2==0?64:16);
   for(int key=50;key<74;key+=2)e.noteOn(key,100);
   for(int i=0;i<5;i++){
     e.render(buffer.data(),256);
     for(float sample:buffer)if(!std::isfinite(sample))return 13;
   }
   e.allNotesOff();e.setLayerInstrument(0,{});
   for(int i=0;i<12;i++)e.render(buffer.data(),256);   // 18 ms all-notes-off fade
   e.collectRetiredInstruments();
   if(!ref.expired()){
      std::cerr<<"Lost PCM reference at cycle "<<round<<"\n";
      return 14;
   }
 }
 std::cout<<"PASS "<<outputRate<<" Hz: loop seam "<<maxAdjacent<<", finite DSP, 120 load/unload cycles, no retained PCM bank\n";
}
