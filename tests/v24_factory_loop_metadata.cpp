// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// Deterministic Nord-format region/loop tests, no sound card or copyrighted samples.
#include "../src/loaders/NsmpRegionDecoder.h"
#include "../src/modules/NordPianoEngine.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
using namespace NordDSP;
static void be32(uint8_t* p,uint32_t v){p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);}
static void regionUnit(){
    uint8_t hdr[0x40]{};
    be32(hdr+0x12,500);be32(hdr+0x1b,600);be32(hdr+0x24,900);be32(hdr+0x2d,1000);
    hdr[0x28]=0x80;hdr[0x29]=0;hdr[0x2a]=0; // signed Q23 = -1.0: exact hold
    std::vector<NsmpStreamMarker> marks={{130,600,false},{430,3000,true},{530,4000,false}};
    const auto r=decodeNsmpRegions(hdr,sizeof(hdr),2000,2,marks);
    assert(r.headerValid && r.hasLoop && !r.oneShot);
    assert(r.alternateStart==300 && r.loopStart==1500);
    assert(r.decayCoef==1.f && r.decayDbPerSecond==0.f);
    hdr[0x29]=1;hdr[0x2a]=0;
    const auto dec=decodeNsmpRegions(hdr,sizeof(hdr),2000,2,marks);
    assert(dec.hasLoop && dec.decayCoef<1.f && dec.decayDbPerSecond<0.f);
    be32(hdr+0x24,1000); // one-shot explicit, not an automatic-loop request
    const auto oneshot=decodeNsmpRegions(hdr,sizeof(hdr),2000,2,marks);
    assert(oneshot.headerValid && oneshot.oneShot && !oneshot.hasLoop);
    be32(hdr+0x24,900);
    const auto missing=decodeNsmpRegions(hdr,sizeof(hdr),2000,2,{{130,600,false},{530,4000,false}});
    assert(missing.headerValid && !missing.hasLoop); // never guess a compressed offset
    const auto bad=decodeNsmpRegions(hdr,sizeof(hdr),2000,2,{{130,600,false},{400,3000,true},{999,4000,false}});
    assert(bad.hasLoop); // supported unshifted fallback
}
static std::shared_ptr<PianoInstrument> makeFactory(){
    auto bank=std::make_shared<PianoInstrument>();bank->formatType="NORD SAMPLE (.nsmp4)";
    StrokeSample s;s.frames=7000;s.sampleRate=48000;s.index=0;s.lokey=0;s.hikey=127;
    s.actualRootMidi=69;s.sampleScale=1.f/32768.f;
    s.loop={true,false,1200,2400,3600};s.decayCoef=1.f;s.loopDecayDbPerS=0.f;
    s.pcmL.resize(s.frames);s.pcmR.resize(s.frames);
    for(uint32_t i=0;i<s.frames;++i){
        // 200 Hz = exactly 240 frames; sample after the *authored* loop is
        // intentionally invalid and must never be read by Hermite's k1/k2.
        const auto x=i>=3600?30000:int(std::lrint(14000.*std::sin(2.*3.141592653589793*200.*i/48000.)));
        s.pcmL[i]=s.pcmR[i]=int16_t(x);
    }
    s.readyFrames->store(s.frames,std::memory_order_release);
    bank->strokes.push_back(std::move(s));return bank;
}
static void audible(double sr){
    NordPianoEngine e;e.setSampleRate(sr);
    e.multiSynth_.synthEnabled=false;e.organModule_.organEnabled=false;
    for(int k=1;k<PIANO_LAYER_COUNT;++k)e.setPianoLayerEnabled(k,false);
    e.setPianoEnabled(true);e.setPianoLayerSampleParam(0,0,.005f);
    e.setPianoLayerSampleParam(0,2,1.f);e.setStudioSlot(3,0,0,0);
    e.setLayerInstrument(0,makeFactory());
    std::vector<float> buf(256*2);
    e.noteOn(69,120);
    float prev=0,maxDelta=0;double eEarly=0,eLate=0;
    const int blocks=int(sr*4/256);
    for(int j=0;j<blocks;++j){
        e.render(buf.data(),256);
        for(int k=0;k<256;++k){
            const float v=buf[size_t(k)*2];
            assert(std::isfinite(v));
            if(j>4)maxDelta=std::max(maxDelta,std::abs(v-prev));
            prev=v;
            if(j>int(sr/256*.8) && j<int(sr/256*1.2))eEarly+=v*v;
            if(j>int(sr/256*3.3) && j<int(sr/256*3.7))eLate+=v*v;
        }
    }
    assert(maxDelta<.19f && "Periodic factory loop must not produce a click");
    assert(eLate>eEarly*.7 && eLate<eEarly*1.3 && "Q23 hold must preserve RMS");
    std::cout<<"PASS factory loop "<<sr<<" Hz, max adjacent delta="<<maxDelta<<"\n";
}
static void fourFrameLoop(){
    auto bank=std::make_shared<PianoInstrument>();bank->formatType="NORD PIANO (.npno)";
    StrokeSample s;s.index=0;s.frames=4;s.sampleRate=48000;s.actualRootMidi=69;
    s.lokey=0;s.hikey=127;s.sampleScale=1.f/32768.f;
    s.loop={true,false,0,4,4};s.decayCoef=1.f;
    s.pcmL={0,14000,0,-14000};s.pcmR=s.pcmL;
    s.readyFrames->store(4,std::memory_order_release);
    bank->strokes.push_back(std::move(s));
    NordPianoEngine e;e.setSampleRate(48000);e.multiSynth_.synthEnabled=false;
    e.organModule_.organEnabled=false;e.setPianoEnabled(true);
    for(int k=1;k<PIANO_LAYER_COUNT;++k)e.setPianoLayerEnabled(k,false);
    e.setLayerInstrument(0,bank);
    std::vector<float> out(256*2);e.noteOn(69,127);
    double power=0;
    for(int block=0;block<50;++block){
        e.render(out.data(),256);
        for(float x:out){assert(std::isfinite(x));power+=x*x;}
    }
    assert(power>1e-4);
    assert(e.getActiveVoiceCount()>0 && "Four-frame factory loop must wrap, not exhaust streaming prefix");
    std::cout<<"PASS four-frame factory loop at sample end\n";
}
static void extras(){
    auto b=makeFactory();b->formatType="Nord Piano v6";b->sourcePath="fixture.npno";
    StrokeSample alt=b->strokes.front();alt.index=1;alt.flags=1;alt.zone=0;
    b->strokes.front().zone=0;
    b->strokes.push_back(std::move(alt));
    StrokeSample release=b->strokes.front();release.index=2;release.flags=2;
    release.isReleaseStroke=true;release.explicitOneShot=true;release.loop.hasLoop=false;
    b->strokes.push_back(std::move(release));
    NordPianoEngine engine;engine.setSampleRate(48000);
    engine.multiSynth_.synthEnabled=false;engine.organModule_.organEnabled=false;
    for(int i=1;i<PIANO_LAYER_COUNT;++i)engine.setPianoLayerEnabled(i,false);
    engine.setLayerInstrument(0,b);engine.setPianoEnabled(true);
    std::vector<float> out(256*2);
    engine.noteOn(69,100);engine.render(out.data(),256);
    assert(engine.getLastStrokeIndex()==0);
    engine.noteOff(69);engine.render(out.data(),256);
    assert(engine.getLastStrokeIndex()==0 && "NPno auxiliary key-up stroke must not retrigger a piano");
    engine.noteOn(69,110);engine.render(out.data(),256);
    assert(engine.getLastStrokeIndex()==0 && "NPno alternative stroke must not replace a velocity layer");
    engine.noteOff(69);engine.render(out.data(),256);
    assert(engine.getLastStrokeIndex()==0);
    engine.noteOn(69,12);engine.render(out.data(),256);
    assert(engine.getLastStrokeIndex()==0 && "Soft velocity should select only a normal stroke");
    // A prolonged sustain chord/note must not introduce a new auxiliary
    // voice when its physical key is released, nor a synthetic second attack.
    engine.allNotesOff();engine.render(out.data(),256);
    engine.setSustainPedal(true);
    engine.noteOn(69,24);engine.render(out.data(),256);
    const int voicesAtAttack=engine.getActiveVoiceCount();
    for(int block=0;block<150;++block)engine.render(out.data(),256);
    const int voicesBeforeOff=engine.getActiveVoiceCount();
    engine.noteOff(69);engine.render(out.data(),256);
    const int voicesAfterOff=engine.getActiveVoiceCount();
    assert(voicesAtAttack>=1 && voicesBeforeOff>=1);
    assert(voicesAfterOff<=voicesBeforeOff && "Key-up added an unrequested auxiliary stroke");
    assert(engine.getLastStrokeIndex()==0);
    engine.setSustainPedal(false);engine.render(out.data(),256);
    std::cout<<"PASS NPno conservative original-stroke playback\n";
}
int main(){regionUnit();fourFrameLoop();audible(44100);audible(48000);audible(96000);extras();std::cout<<"PASS NSMP regions + exact hold + short loop + release\n";}
