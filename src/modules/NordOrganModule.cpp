#include "NordOrganModule.h"
#include "NordDspEffects.h"
#include <cmath>

namespace NordDSP {

void NordOrganModule::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    for (auto& v : organVoices) v.active = false;
}

void NordOrganModule::noteOn(int rawKey, int playedKey, int /*velocity*/) {
    if (!organEnabled) return;
    SynthPolyVoice* target = nullptr;
    for (auto& v : organVoices) {
        if (v.active && v.rawKey == rawKey) { target = &v; break; }
    }
    if (!target) {
        for (auto& v : organVoices) {
            if (!v.active) { target = &v; break; }
        }
    }
    if (!target) target = &organVoices[0];

    target->active = true;
    target->rawKey = rawKey;
    target->keyHeld = true;
    target->sustained = false;
    target->stage = 3;
    target->env = 1.0f;
    target->freq = 440.0 * std::pow(2.0, (playedKey - 69.0) / 12.0);
}

void NordOrganModule::noteOff(int rawKey, bool pedalDown) {
    for (auto& v : organVoices) {
        if (v.active && v.rawKey == rawKey && v.keyHeld) {
            v.keyHeld = false;
            if (pedalDown) v.sustained = true;
            else v.active = false;
        }
    }
}

void NordOrganModule::setSustain(bool down) {
    if (!down) {
        for (auto& v : organVoices) {
            if (v.active && !v.keyHeld && v.sustained) {
                v.sustained = false;
                v.active = false;
            }
        }
    }
}

void NordOrganModule::allNotesOff() {
    for (auto& v : organVoices) v.active = false;
}

void NordOrganModule::render(float* outInterleavedStereo, unsigned int numFrames) {
    if (!organEnabled || organVolume <= 0.001f) return;

    static constexpr double HARMONICS[9] = {0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
    const float rotRate = organRotaryFast ? 6.2f : 0.85f;

    for (unsigned int n = 0; n < numFrames; ++n) {
        float orgSum = 0.0f;
        for (auto& v : organVoices) {
            if (!v.active) continue;
            double p = v.phase1;
            float noteSample = 0.0f;
            for (int d = 0; d < 9; ++d) {
                if (drawbars[d] > 0.01f) {
                    noteSample += std::sin(2.0f * PI_F * static_cast<float>(p * HARMONICS[d])) * drawbars[d];
                }
            }
            v.phase1 += v.freq / sr_;
            if (v.phase1 >= 2.0) v.phase1 -= 2.0;
            orgSum += noteSample * 0.045f;
        }

        if (organDrive > 0.01f) {
            orgSum = std::tanh(orgSum * (1.0f + organDrive * 4.0f));
        }

        rotaryPhase_ += rotRate / sr_;
        if (rotaryPhase_ >= 1.0f) rotaryPhase_ -= 1.0f;
        float panL = 0.75f + 0.25f * std::sin(2.0f * PI_F * rotaryPhase_);
        float panR = 0.75f + 0.25f * std::cos(2.0f * PI_F * rotaryPhase_);

        outInterleavedStereo[n * 2]     += orgSum * panL * organVolume;
        outInterleavedStereo[n * 2 + 1] += orgSum * panR * organVolume;
    }
}

} // namespace NordDSP