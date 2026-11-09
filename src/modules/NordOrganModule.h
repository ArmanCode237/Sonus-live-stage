#pragma once
#include "NordTypes.h"
#include "NordSynthModule.h"
#include <array>

namespace NordDSP {

class NordOrganModule {
public:
    bool organEnabled = false;
    int organModel = 0;
    float organVolume = 0.75f;
    bool organRotaryFast = false;
    float organDrive = 0.15f;
    std::array<float, 9> drawbars = {0.8f, 0.6f, 0.9f, 0.4f, 0.2f, 0.3f, 0.1f, 0.1f, 0.2f};
    std::array<SynthPolyVoice, 24> organVoices;

    void init(float sr);
    void noteOn(int rawKey, int playedKey, int velocity);
    void noteOff(int rawKey, bool pedalDown);
    void setSustain(bool down);
    void allNotesOff();
    void render(float* outInterleavedStereo, unsigned int numFrames);

private:
    float sr_ = 48000.0f;
    float rotaryPhase_ = 0.0f;
};

} // namespace NordDSP