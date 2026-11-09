#pragma once
#include "NordTypes.h"
#include "NordDspEffects.h"
#include <array>

namespace NordDSP {

struct SynthPolyVoice {
    bool active = false;
    int rawKey = -1;
    bool keyHeld = false;
    bool sustained = false;
    int stage = 0; // 0=Idle, 1=Attack, 2=Decay, 3=Sustain, 4=Release
    float env = 0.0f;
    float velGain = 1.0f;
    double phase1 = 0.0, phase2 = 0.23, phase3 = 0.61;
    double freq = 440.0;
};

class SingleSynthLayer {
public:
    SynthLayerConfig cfg;
    StateVariableFilter filter;
    std::array<SynthPolyVoice, 24> voices;

    void reset();
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr, float macroCutoff);
};

class NordSynthModule {
public:
    bool synthEnabled = false;
    int synthPresetId = 1;
    float synthMasterVol = 0.80f;
    float synthMacroCutoff = 1.0f;
    std::array<SingleSynthLayer, 3> layers;

    void init(float sr);
    void loadBuiltInSynthPreset(int presetId);
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames);

private:
    float sr_ = 48000.0f;
};

} // namespace NordDSP