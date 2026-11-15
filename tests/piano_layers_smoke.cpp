#include "../src/modules/NordPianoEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace NordDSP;
static std::shared_ptr<PianoInstrument> makeBank(int id) {
    auto bank = std::make_shared<PianoInstrument>();
    bank->id = id;
    bank->name = "Test Piano " + std::to_string(id);
    bank->sourcePath = "test_bank_" + std::to_string(id);
    StrokeSample sample;
    sample.frames = 4096;
    sample.sampleRate = 48000;
    sample.actualRootMidi = 60;
    sample.lokey = 0;
    sample.hikey = 127;
    sample.pcmL.resize(sample.frames);
    sample.pcmR.resize(sample.frames);
    for (size_t i = 0; i < sample.frames; ++i) {
        const float value = 0.2f * std::sin(2.0f * 3.14159265f * i * (i + id) / 48000.0f);
        sample.pcmL[i] = int16_t(std::lrint(32767.f*value));
        sample.pcmR[i] = int16_t(std::lrint(32767.f*value));
    }
    sample.sampleScale=1.f/32768.f;sample.readyFrames->store(sample.frames);
    bank->strokes.push_back(std::move(sample));
    return bank;
}

int main() {
    NordPianoEngine engine;
    engine.setSampleRate(48000);
    engine.setReverbEnabled(false);
    engine.setEqEnabled(false);
    engine.setDelayEnabled(false);
    for(int i=0; i<3; ++i) {
        engine.setLayerInstrument(i, makeBank(i+1));
        engine.setPianoLayerEnabled(i, true);
    }
    assert(engine.getPianoInstruments()[0] && engine.getPianoInstruments()[1] && engine.getPianoInstruments()[2]);
    std::vector<float> audio(1024);
    engine.noteOn(60, 110);
    engine.render(audio.data(),512);
    assert(engine.getActiveVoiceCount() == 3);
    double energy = 0;
    for(float v:audio) { assert(std::isfinite(v)); energy += v*v; }
    assert(energy > 1e-8);
    std::cout << "Three samples triggered in parallel: OK, energy=" << energy << "\n";

    engine.allNotesOff();
    engine.render(audio.data(),512);
    engine.setPianoLayerEnabled(2,false);
    engine.noteOn(60,110);
    engine.render(audio.data(),512);
    assert(engine.getActiveVoiceCount() == 2);
    std::cout << "Per-layer on/off: OK\n";

    engine.allNotesOff();
    engine.render(audio.data(),512);
    engine.setPianoVolume(0.0f);
    engine.noteOn(60,110);
    engine.render(audio.data(),512);
    double mutedEnergy = 0;
    for(float v:audio) { assert(std::isfinite(v)); mutedEnergy += v*v; }
    assert(mutedEnergy < 1e-7);
    std::cout << "Piano module volume mute: OK\n";

    // Replace the sample bank while its voices are active. They must keep ownership.
    engine.setPianoVolume(1.0f);
    engine.setLayerInstrument(0, makeBank(12));
    engine.render(audio.data(),512);
    for(float v:audio) assert(std::isfinite(v));
    assert(engine.getPianoInstruments()[0]->id == 12);
    std::cout << "Hot bank replacement with held voices: OK\n";

    auto params=engine.getCurrentParams();
    assert(params.pianoVolume == 1.0f && !params.pianoLayerEnabled[2]);
    params.pianoLayerEnabled[2] = true;
    params.pianoLayerVolume[1] = 0.55f;
    engine.applyPreset(params);
    assert(engine.getCurrentParams().pianoLayerEnabled[2]);
    assert(std::fabs(engine.getCurrentParams().pianoLayerVolume[1] - .55f) < 1e-5f);
    std::cout << "Global patch piano-layer parameter roundtrip: OK\n";
    return 0;
}
