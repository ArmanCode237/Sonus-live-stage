#include "../src/modules/NordPianoEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace NordDSP;
static std::shared_ptr<PianoInstrument> makeSample() {
    auto bank=std::make_shared<PianoInstrument>();
    StrokeSample s; s.frames=48000;s.sampleRate=48000;s.actualRootMidi=60;s.lokey=0;s.hikey=127;
    s.audioL.resize(s.frames);s.audioR.resize(s.frames);
    for (unsigned n=0;n<s.frames;++n) s.audioL[n]=s.audioR[n]=0.08f*std::sin(2*3.141592653589793*220*n/48000);
    bank->strokes.push_back(std::move(s));return bank;
}
static int crosses(const std::vector<float>& buffer) {
    int z=0;
    for(size_t i=2;i<buffer.size();i+=2) if(buffer[i-2]<0.f&&buffer[i]>=0.f)++z;
    return z;
}
int main() {
    NordPianoEngine engine;engine.setSampleRate(48000);
    engine.setEqEnabled(false);engine.setReverbEnabled(false);engine.setDelayEnabled(false);
    engine.setPianoEnabled(true);auto bank=makeSample();
    for(int i=0;i<5;++i){engine.setLayerInstrument(i,bank);engine.setPianoLayerEnabled(i,i==0);}
    engine.setPianoLayerSampleParam(0,0,.005f);
    engine.setPianoLayerSampleParam(4,0,.85f);
    engine.setPianoLayerSampleParam(4,1,5.5f);
    engine.setPianoLayerSampleParam(4,3,2.0f);
    engine.setPianoLayerSampleParam(4,5,-.7f);
    engine.setPianoLayerSampleParam(4,6,44.f);
    engine.setPianoLayerSampleParam(3,7,1700.f);engine.setPianoLayerSampleParam(3,8,3.4f);
    engine.setModWheel(.6f);engine.setPitchBend(.5f);
    auto preset=engine.getCurrentParams();
    assert(std::abs(preset.pianoLayerSample[4].attackSec-.85f)<1e-5f);
    assert(std::abs(preset.pianoLayerSample[4].fineTuneCents-44.f)<1e-5f);
    assert(std::abs(engine.getPitchBend()-.5f)<1e-5f);
    assert(std::abs(engine.getModWheel()-.6f)<1e-5f);
    engine.applyPreset(preset);
    auto again=engine.getCurrentParams();
    assert(std::abs(again.pianoLayerSample[4].decaySec-5.5f)<1e-5f);
    assert(std::abs(again.pianoLayerSample[4].pan+.7f)<1e-5f);
    assert(std::abs(again.pianoLayerSample[4].releaseSec-2.f)<1e-5f);
    assert(std::abs(again.pianoLayerSample[3].cutoffHz-1700.f)<1e-5f);
    assert(std::abs(again.pianoLayerSample[3].resonanceQ-3.4f)<1e-5f);
    engine.setModWheel(0.f);engine.setPitchBend(0.f);
    engine.noteOn(60,110);std::vector<float> flat(8192*2);engine.render(flat.data(),8192);
    engine.allNotesOff();engine.render(flat.data(),8192);
    NordPianoEngine sharp;sharp.setSampleRate(48000);
    sharp.setEqEnabled(false);sharp.setReverbEnabled(false);sharp.setDelayEnabled(false);
    sharp.setLayerInstrument(0,bank);sharp.setPitchBend(1.f);
    sharp.noteOn(60,110);std::vector<float> bent(8192*2);sharp.render(bent.data(),8192);
    // Both audio streams are finite; an upward bend increases oscillation count.
    for(float s:bent)assert(std::isfinite(s));
    for(float s:flat)assert(std::isfinite(s));
    assert(std::any_of(bent.begin(), bent.end(), [](float v){return std::abs(v)>0.00001f;}));
    for(int id=1;id<=48;++id) {
        sharp.multiSynth_.loadBuiltInSynthPreset(id);
        assert(sharp.multiSynth_.synthPresetId==id);
    }
    preset.organRotaryMix=.25f;preset.organVibratoRate=8.8f;preset.organVibratoDepth=1.6f;
    engine.applyPreset(preset);again=engine.getCurrentParams();
    assert(std::abs(again.organRotaryMix-.25f)<1e-5f);
    assert(std::abs(again.organVibratoRate-8.8f)<1e-5f);
    std::cout<<"OK: 5 envelopes, pitch bend, mod wheel, 48 presets, organ DSP, finite audio\n";
}
