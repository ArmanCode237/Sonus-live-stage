#include "../src/modules/StagePianoEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
using namespace SonusDSP;
std::shared_ptr<PianoInstrument> testBank(int id) {
    auto b = std::make_shared<PianoInstrument>();
    b->id = id; b->name = "piano-" + std::to_string(id);
    StrokeSample s; s.frames=16000; s.sampleRate=48000; s.actualRootMidi=60;
    s.lokey=0; s.hikey=127;
    s.pcmL.resize(s.frames); s.pcmR.resize(s.frames);
    for(unsigned k=0;k<s.frames;++k)
        s.pcmL[k]=s.pcmR[k]=int16_t(std::lrint(32767.f*0.06f*std::sin(2.0*3.141592653589793*k*220.0/48000.0)));
    s.sampleScale=1.f/32768.f;s.readyFrames->store(s.frames);b->strokes.push_back(std::move(s)); return b;
}
int main() {
  StagePianoEngine piano;
  piano.setSampleRate(48000);
  piano.setEqEnabled(false); piano.setReverbEnabled(false);
  piano.setChorusEnabled(false); piano.setPhaserEnabled(false); piano.setDelayEnabled(false);
  for(int i=0;i<PIANO_LAYER_COUNT;++i){ piano.setLayerInstrument(i,testBank(i+1));piano.setPianoLayerEnabled(i,true); }
  for(int i=0;i<FX_SOURCE_COUNT;++i)for(int bus=0;bus<FX_BUS_COUNT;++bus) piano.setFxSend(i,bus,0);
  piano.noteOn(60,120);
  std::vector<float> buffer(2048); piano.render(buffer.data(),1024);
  assert(piano.getActiveVoiceCount()==5);
  double e=0;
  for(float x:buffer) {assert(std::isfinite(x));e+=x*x;}
  assert(e>1e-5);assert(piano.getPianoInstruments()[4]->id==5);
  auto snap = piano.getCurrentParams();
  assert(!snap.fxSends[4].reverb && snap.pianoLayerEnabled[4]);
  snap.fxSends[4].reverb=.4f; snap.fxSends[7].modulation=.75f;
  snap.pianoLayerVolume[4]=.63f;
  piano.applyPreset(snap);
  auto recalled=piano.getCurrentParams();
  assert(std::abs(recalled.fxSends[4].reverb - .4f)<1e-5f);
  assert(std::abs(recalled.fxSends[7].modulation - .75f)<1e-5f);
  assert(std::abs(recalled.pianoLayerVolume[4] - .63f)<1e-5f);
  piano.render(buffer.data(),1024);
  for(float x:buffer)assert(std::isfinite(x));
  std::cout<<"OK five piano voices, 9x3 FX send params, roundtrip, finite audio. energy="<<e<<"\n";
}
