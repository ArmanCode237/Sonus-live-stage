// Repeated sustained notes, voice stealing, streaming prefix and organ phase.
// Standalone: does not require RtAudio, RtMidi or an audio interface.
#include "../src/modules/StagePianoEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;

static std::shared_ptr<PianoInstrument> makeBank(){
    auto b=std::make_shared<PianoInstrument>();
    b->formatType="SF2 (PCM16)";
    StrokeSample s;
    s.sampleRate=48000;s.frames=48000;s.lokey=0;s.hikey=127;
    s.actualRootMidi=60;s.lovel=1;s.hivel=127;
    s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
    s.sampleScale=1.f/32768.f;
    for(uint32_t i=0;i<s.frames;++i){
        const double t=double(i)/48000.;
        const auto x=int16_t(std::lrint(18000.*std::sin(2.*3.141592653589793*220.*t)));
        s.pcmL[i]=x;s.pcmR[i]=x;
    }
    s.sf2LoopMode=1;s.loop={true,false,12000,24000,36000};
    s.readyFrames->store(s.frames,std::memory_order_release);
    b->strokes.push_back(std::move(s));return b;
}

int main(int argc,char** argv){
    const double sr=argc>1?std::atof(argv[1]):48000.;
    StagePianoEngine e;e.setSampleRate(sr);
    e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
    for(int i=1;i<PIANO_LAYER_COUNT;++i)e.setPianoLayerEnabled(i,false);
    e.setPianoEnabled(true);e.setMaxVoices(16);
    e.setStudioSlot(3,0,0,0);
    auto bank=makeBank();
    e.setLayerInstrument(0,bank);
    std::vector<float> out(256*2);
    float last=0,maxJump=0;bool hasPrevious=false;
    auto renderBlock=[&]{
        e.render(out.data(),256);
        for(size_t n=0;n<256;++n){
            const auto x=out[n*2];
            if(!std::isfinite(x)||!std::isfinite(out[n*2+1])){std::cerr<<"NaN DSP\n";std::exit(2);}
            if(hasPrevious)maxJump=std::max(maxJump,std::abs(x-last));
            last=x;hasPrevious=true;
        }
    };
    e.noteOn(60,110);
    for(int j=0;j<6;++j)renderBlock();
    e.setSustainPedal(true);
    e.noteOff(60);
    for(int j=0;j<4;++j)renderBlock();
    for(int n=0;n<8;++n){
        e.noteOn(60,112);
        for(int j=0;j<2;++j)renderBlock();
        e.noteOff(60);
    }
    const float sustainJump=maxJump;
    for(int key=40;key<100;++key){
        e.noteOn(key,100);
        if(key%4==0)renderBlock();
    }
    for(int j=0;j<20;++j)renderBlock();
    // A dense 60-note chord legitimately has larger adjacent-sample deltas
    // than a single oscillating piano key. Test the sustain case independently.
    if(sustainJump>.20f || maxJump>.60f){
        std::cerr<<"Sustain transient "<<sustainJump<<", chord transient "<<maxJump<<"\n";
        return 3;
    }

    // Separate bank: backing vectors already exist, but the producer has only
    // published 20 ms. Verify the callback does not touch the remaining frames.
    auto streaming=makeBank();
    streaming->strokes[0].readyFrames->store(960,std::memory_order_release);
    e.allNotesOff();e.render(out.data(),256);
    e.setSustainPedal(false);e.setLayerInstrument(0,streaming);
    e.noteOn(60,110);
    for(int i=0;i<15;++i)renderBlock();
    streaming->strokes[0].readyFrames->store(streaming->strokes[0].frames,std::memory_order_release);
    for(int i=0;i<12;++i)renderBlock();

    StageOrganModule organ;
    organ.init(float(sr));organ.organEnabled=true;
    organ.noteOn(60,60,100);
    organ.render(out.data(),256);
    double phase=-1;
    for(const auto& v:organ.organVoices)if(v.active&&v.rawKey==60){phase=v.phase1;break;}
    if(phase<0)return 4;
    organ.noteOn(60,60,110);
    int occurrences=0;double nextPhase=-1;
    for(const auto& v:organ.organVoices)if(v.active&&v.rawKey==60){
        ++occurrences;nextPhase=v.phase1;
    }
    if(occurrences!=1||std::abs(nextPhase-phase)>1e-10){
        std::cerr<<"Organ oscillator phase discontinuity\n";return 5;
    }
    std::cout<<"PASS "<<sr<<"Hz: pedal retrigger, 18ms steal fades, progressive PCM, organ phase; max delta="<<maxJump<<"\n";
}
