#include "NordSynthModule.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace NordDSP {

void SingleSynthLayer::reset() {
    filter.reset();
    for (auto& v : voices) v.active = false;
}

void SingleSynthLayer::noteOn(int rawKey, int playedKey, int velocity) {
    if (!cfg.enabled) return;
    SynthPolyVoice* target = nullptr;
    for (auto& v : voices) {
        if (v.active && v.rawKey == rawKey) { target = &v; break; }
    }
    if (!target) {
        for (auto& v : voices) {
            if (!v.active) { target = &v; break; }
        }
    }
    if (!target) target = &voices[0];

    double pitchNote = static_cast<double>(playedKey) +
                       cfg.octave * 12.0 +
                       cfg.semitones +
                       (cfg.detuneCents / 100.0);

    target->active = true;
    target->rawKey = rawKey;
    target->keyHeld = true;
    target->sustained = false;
    target->stage = 1;
    target->env = 0.0f;
    target->velGain = 0.25f + (static_cast<float>(velocity) / 127.0f) * 0.75f;
    target->freq = 440.0 * std::pow(2.0, (pitchNote - 69.0) / 12.0);
}

void SingleSynthLayer::noteOff(int rawKey, bool pedalDown) {
    for (auto& v : voices) {
        if (v.active && v.rawKey == rawKey && v.keyHeld) {
            v.keyHeld = false;
            if (pedalDown) v.sustained = true;
            else v.stage = 4;
        }
    }
}

void SingleSynthLayer::setSustain(bool down) {
    if (!down) {
        for (auto& v : voices) {
            if (v.active && !v.keyHeld && v.sustained) {
                v.sustained = false;
                v.stage = 4;
            }
        }
    }
}

void SingleSynthLayer::renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr, float macroCutoff) {
    if (!cfg.enabled || cfg.volume <= 0.001f) return;

    float effCutoff = std::clamp(cfg.cutoffHz * macroCutoff, 40.0f, 20000.0f);
    filter.setParams(sr, effCutoff, cfg.resonanceQ);

    const float atkStep = 1.0f / (std::max(0.001f, cfg.attackSec) * sr);
    const float decCoef = std::exp(-4.6f / (std::max(0.005f, cfg.decaySec) * sr));
    const float relCoef = std::exp(-6.9f / (std::max(0.005f, cfg.releaseSec) * sr));
    const float susLvl  = std::clamp(cfg.sustainLevel, 0.0f, 1.0f);

    const float panAngle = (std::clamp(cfg.pan, -1.0f, 1.0f) + 1.0f) * (PI_F * 0.25f);
    const float gainL = std::cos(panAngle) * cfg.volume * 0.22f;
    const float gainR = std::sin(panAngle) * cfg.volume * 0.22f;

    for (unsigned int n = 0; n < numFrames; ++n) {
        float sum = 0.0f;
        for (auto& v : voices) {
            if (!v.active) continue;
            switch (v.stage) {
                case 1:
                    v.env += atkStep;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; }
                    break;
                case 2:
                    v.env = susLvl + (v.env - susLvl) * decCoef;
                    if (std::abs(v.env - susLvl) < 1e-4f) { v.env = susLvl; v.stage = 3; }
                    break;
                case 3:
                    v.env = susLvl;
                    break;
                case 4:
                    v.env *= relCoef;
                    if (v.env < 1e-4f) { v.env = 0.0f; v.active = false; }
                    break;
            }
            if (!v.active) continue;

            double inc = v.freq / sr;
            float osc = 0.0f;
            switch (cfg.waveType) {
                case 0: osc = static_cast<float>(2.0 * v.phase1 - 1.0); break;
                case 1: osc = (v.phase1 < 0.5) ? 0.75f : -0.75f; break;
                case 2: osc = static_cast<float>(4.0 * std::abs(v.phase1 - 0.5) - 1.0); break;
                case 3: osc = std::sin(2.0f * PI_F * static_cast<float>(v.phase1)); break;
                case 4: {
                    float s1 = static_cast<float>(2.0 * v.phase1 - 1.0);
                    float s2 = static_cast<float>(2.0 * v.phase2 - 1.0);
                    float s3 = static_cast<float>(2.0 * v.phase3 - 1.0);
                    osc = (s1 + s2 * 0.8f + s3 * 0.8f) * 0.45f;
                    v.phase2 += inc * 1.006; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                    v.phase3 += inc * 0.994; if (v.phase3 >= 1.0) v.phase3 -= 1.0;
                    break;
                }
                case 5: osc = ((std::rand() & 0x7FFF) / 16384.0f - 1.0f) * 0.5f; break;
            }
            v.phase1 += inc;
            if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            sum += osc * v.env * v.velGain;
        }

        float fL = sum * gainL;
        float fR = sum * gainR;
        filter.processStereo(fL, fR);
        mixL[n] += fL;
        mixR[n] += fR;
    }
}

void NordSynthModule::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    for (auto& l : layers) l.reset();
    loadBuiltInSynthPreset(synthPresetId);
}

void NordSynthModule::loadBuiltInSynthPreset(int presetId) {
    synthPresetId = std::clamp(presetId, 1, 8);
    for (auto& l : layers) l.reset();

    if (synthPresetId == 1) {
        layers[0].cfg = {true,  4, 0.80f, -0.20f,  0, 0, -6.0f, 4800.0f, 0.8f, 0.18f, 2.2f, 0.85f, 0.95f};
        layers[1].cfg = {true,  0, 0.65f,  0.20f, -1, 0,  6.0f, 3600.0f, 0.9f, 0.22f, 2.5f, 0.80f, 1.10f};
        layers[2].cfg = {true,  3, 0.55f,  0.00f, +1, 0,  0.0f, 9000.0f, 0.7f, 0.35f, 2.8f, 0.90f, 1.30f};
    } else if (synthPresetId == 2) {
        layers[0].cfg = {true,  0, 0.85f, -0.25f,  0, 0, -9.0f, 7500.0f, 1.4f, 0.035f, 0.9f, 0.70f, 0.35f};
        layers[1].cfg = {true,  0, 0.85f,  0.25f,  0, 0,  9.0f, 7500.0f, 1.4f, 0.045f, 0.9f, 0.70f, 0.35f};
        layers[2].cfg = {true,  1, 0.50f,  0.00f, -1, 0,  0.0f, 4200.0f, 1.0f, 0.020f, 1.2f, 0.65f, 0.40f};
    } else if (synthPresetId == 3) {
        layers[0].cfg = {true,  4, 0.90f, -0.30f,  0, 0, -12.0f, 14000.0f, 1.2f, 0.005f, 1.5f, 0.90f, 0.50f};
        layers[1].cfg = {true,  4, 0.85f,  0.30f, +1, 0,  12.0f, 12000.0f, 1.1f, 0.008f, 1.5f, 0.85f, 0.55f};
        layers[2].cfg = {true,  1, 0.65f,  0.00f, -1, 0,   0.0f,  5500.0f, 0.9f, 0.005f, 1.8f, 0.80f, 0.45f};
    } else if (synthPresetId == 4) {
        layers[0].cfg = {true,  2, 0.85f, -0.15f, +1, 0, -4.0f, 11000.0f, 1.6f, 0.002f, 0.45f, 0.15f, 0.65f};
        layers[1].cfg = {true,  3, 0.75f,  0.15f, +2, 7,  4.0f, 15000.0f, 0.8f, 0.002f, 0.35f, 0.10f, 0.75f};
        layers[2].cfg = {false, 0, 0.50f,  0.00f,  0, 0,  0.0f,  8000.0f, 0.7f, 0.010f, 1.00f, 0.50f, 0.40f};
    } else {
        layers[0].cfg = {true,  4, 0.78f, -0.25f,  0, 0, -10.0f, 6800.0f, 0.8f, 0.28f, 2.0f, 0.90f, 1.20f};
        layers[1].cfg = {true,  2, 0.70f,  0.25f,  0, 0,  10.0f, 5400.0f, 0.8f, 0.32f, 2.2f, 0.85f, 1.25f};
        layers[2].cfg = {true,  3, 0.60f,  0.00f, -1, 0,   0.0f, 3200.0f, 0.7f, 0.20f, 2.5f, 0.95f, 1.10f};
    }
}

void NordSynthModule::noteOn(int rawKey, int playedKey, int velocity) {
    if (!synthEnabled) return;
    for (auto& l : layers) l.noteOn(rawKey, playedKey, velocity);
}

void NordSynthModule::noteOff(int rawKey, bool pedalDown) {
    for (auto& l : layers) l.noteOff(rawKey, pedalDown);
}

void NordSynthModule::setSustain(bool down) {
    for (auto& l : layers) l.setSustain(down);
}

void NordSynthModule::allNotesOff() {
    for (auto& l : layers) l.reset();
}

void NordSynthModule::render(float* outInterleavedStereo, unsigned int numFrames) {
    if (!synthEnabled || synthMasterVol <= 0.001f) return;
    std::vector<float> bufL(numFrames, 0.0f);
    std::vector<float> bufR(numFrames, 0.0f);

    for (auto& l : layers) {
        l.renderLayer(bufL.data(), bufR.data(), numFrames, sr_, synthMacroCutoff);
    }
    for (unsigned int n = 0; n < numFrames; ++n) {
        outInterleavedStereo[n * 2]     += bufL[n] * synthMasterVol;
        outInterleavedStereo[n * 2 + 1] += bufR[n] * synthMasterVol;
    }
}

} // namespace NordDSP