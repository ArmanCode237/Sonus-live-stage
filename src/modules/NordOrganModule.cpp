#include "NordOrganModule.h"
#include "../dsp/NordDspEffects.h"
#include <cmath>
#include <algorithm>
#include <fstream>

namespace NordDSP {

namespace {
constexpr double HARMONICS[9] = {0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
constexpr float LEAKAGE = 0.008f;
constexpr float CROSSOVER_HZ = 800.0f;
}

void NordOrganModule::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    for (auto& v : organVoices) {
        v.active = false;
        v.env = 0.0f;
        v.stage = 0;
    }
    horn_.phase = 0.0;
    horn_.currentRPS = LESLIE_SLOW_RPS;
    drum_.phase = 0.25;
    drum_.currentRPS = LESLIE_SLOW_RPS;
    drumLP_state_ = 0.0f;
    vibWritePos_ = 0;
    vibLfoPhase_ = 0.0;
    vibDelayLine_.fill(0.0f);
    clickEnvelope_ = 0.0f;
}

bool NordOrganModule::loadOrganSample(const std::string& path) {
    organSamplePath = path;
    organSampleL.clear();
    organSampleR.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char header[44];
    f.read(header, 44);
    int numChannels = header[22] | (header[23] << 8);
    int bitsPerSample = header[34] | (header[35] << 8);
    char sub[4];
    int dataSize = 0;
    while (f.read(sub, 4)) {
        if (sub[0]=='d' && sub[1]=='a' && sub[2]=='t' && sub[3]=='a') {
            f.read(reinterpret_cast<char*>(&dataSize), 4);
            break;
        }
    }
    if (dataSize <= 0) return false;
    std::vector<char> raw(dataSize);
    f.read(raw.data(), dataSize);
    if (bitsPerSample == 16) {
        const int16_t* p = reinterpret_cast<const int16_t*>(raw.data());
        int totalSamples = dataSize / 2;
        if (numChannels == 1) {
            organSampleL.resize(totalSamples);
            for (int i = 0; i < totalSamples; ++i) organSampleL[i] = p[i] / 32768.0f;
            organSampleR = organSampleL;
        } else {
            int frames = totalSamples / 2;
            organSampleL.resize(frames);
            organSampleR.resize(frames);
            for (int i = 0; i < frames; ++i) {
                organSampleL[i] = p[i * 2] / 32768.0f;
                organSampleR[i] = p[i * 2 + 1] / 32768.0f;
            }
        }
    }
    return !organSampleL.empty();
}

void NordOrganModule::noteOn(int rawKey, int playedKey, int velocity) {
    if (!organEnabled) return;

    // En un Hammond B3 real, la percusión es "Single-Triggered" (solo dispara si no había otra tecla retenida)
    bool anyHeldBefore = false;
    OrganVoice* target = nullptr;
    for (auto& v : organVoices) {
        if (v.active && v.keyHeld && v.rawKey != rawKey) anyHeldBefore = true;
        if (v.active && v.rawKey == rawKey) target = &v;
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
    target->velGain = 0.85f + (velocity / 127.0f) * 0.15f; // El órgano es casi constante en velocidad
    target->freq = 440.0 * std::pow(2.0, (playedKey - 69.0) / 12.0);

    if (percussionEnabled && !anyHeldBefore) {
        target->percEnv = 1.0;
        target->percActive = true;
    } else {
        target->percEnv = 0.0;
        target->percActive = false;
    }

    clickEnvelope_ = 0.65f;
    clickSample_ = ((rawKey & 1) ? 1.0f : -1.0f) * 0.18f;
}

void NordOrganModule::noteOff(int rawKey, bool pedalDown) {
    for (auto& v : organVoices) {
        if (v.active && v.rawKey == rawKey && v.keyHeld) {
            v.keyHeld = false;
            if (pedalDown) v.sustained = true;
            else v.stage = 4; // Caída anti-click de 4 ms
        }
    }
}

void NordOrganModule::setSustain(bool down) {
    if (!down) {
        for (auto& v : organVoices) {
            if (v.active && !v.keyHeld && v.sustained) {
                v.sustained = false;
                v.stage = 4;
            }
        }
    }
}

void NordOrganModule::allNotesOff() {
    for (auto& v : organVoices) {
        v.active = false;
        v.keyHeld = false;
        v.sustained = false;
    }
}

void NordOrganModule::render(float* outInterleavedStereo, unsigned int numFrames) {
    if (!organEnabled || organVolume <= 0.001f) return;

    const double hornTarget = organRotaryFast ? LESLIE_FAST_RPS : LESLIE_SLOW_RPS;
    const double drumTarget = organRotaryFast ? LESLIE_FAST_DRUM_RPS : (LESLIE_SLOW_RPS * 0.85);
    const double wc = 2.0 * PI_F * CROSSOVER_HZ / sr_;
    const float lpCoef = static_cast<float>(wc / (wc + 1.0));
    const float releaseStep = 1.0f / (0.0045f * sr_); // 4.5 ms anti-click release
    const double percDecayCoef = std::exp(-4.8 / (std::max(0.08f, percussionDecaySec) * sr_));

    const double vibRateHz = 6.8;
    const double vibInc = vibRateHz / sr_;
    float vibDepthSamples = 0.0f;
    bool useChorus = (vibratoMode >= 4);
    switch (vibratoMode) {
        case 1: case 4: vibDepthSamples = 1.8f; break;
        case 2: case 5: vibDepthSamples = 3.4f; break;
        case 3: case 6: vibDepthSamples = 5.5f; break;
        default: vibDepthSamples = 0.0f; break;
    }

    for (unsigned int n = 0; n < numFrames; ++n) {
        auto updateRotor = [&](LeslieRotor& r, double target) {
            double tau = (target > r.currentRPS) ? r.accelTau : r.decelTau;
            float coef = static_cast<float>(std::exp(-1.0 / (tau * sr_)));
            r.currentRPS = target + coef * (r.currentRPS - target);
            r.phase += r.currentRPS / sr_;
            if (r.phase >= 1.0) r.phase -= 1.0;
        };
        updateRotor(horn_, hornTarget);
        updateRotor(drum_, drumTarget);

        float orgSum = 0.0f;
        float percSum = 0.0f;

        for (auto& v : organVoices) {
            if (!v.active) continue;

            if (v.stage == 4) {
                v.env -= releaseStep;
                if (v.env <= 0.0f) {
                    v.env = 0.0f;
                    v.active = false;
                    continue;
                }
            }

            double p = v.phase1;
            float noteSample = 0.0f;

            for (int d = 0; d < 9; ++d) {
                float level = drawbars[d];
                if (d > 0) level += drawbars[d - 1] * LEAKAGE;
                if (d < 8) level += drawbars[d + 1] * LEAKAGE;
                if (level > 0.005f) {
                    double hp = p * HARMONICS[d];
                    float sig = std::sin(static_cast<float>(2.0 * PI_F * hp));
                    // Carácter según el modelo de órgano seleccionado:
                    // 0 = B3 Tonewheel (Senoidal pura), 1 = Vox (Rica en armónicos pares), 2 = Farfisa (Brillante)
                    if (organModel == 1) {
                        sig = 0.65f * sig + 0.35f * std::sin(static_cast<float>(4.0 * PI_F * hp));
                    } else if (organModel == 2) {
                        sig = std::tanh(sig * 1.9f);
                    }
                    noteSample += sig * level;
                }
            }

            // CORRECCIÓN CLAVE: Envolver en 2.0 para que 0.5 (16') y 1.5 (5 1/3') completen ciclos enteros
            v.phase1 += v.freq / sr_;
            if (v.phase1 >= 2.0) v.phase1 -= 2.0;

            if (percussionEnabled && v.percActive && v.percEnv > 0.001) {
                double percHarm = (percussionHarmonic == 2) ? 2.0 : 3.0;
                float percS = std::sin(static_cast<float>(2.0 * PI_F * p * percHarm));
                percSum += percS * static_cast<float>(v.percEnv) * percussionLevel * v.env;
                v.percEnv *= percDecayCoef;
                if (v.percEnv <= 0.001) v.percActive = false;
            }

            orgSum += noteSample * v.env * v.velGain * 0.055f;
        }

        orgSum += percSum * 0.08f;

        if (clickEnvelope_ > 0.001f) {
            orgSum += clickSample_ * clickEnvelope_;
            clickEnvelope_ *= 0.92f;
        }

        if (organDrive > 0.01f) {
            float pre = 1.0f + organDrive * 4.5f;
            orgSum = std::tanh(orgSum * pre) / std::tanh(pre) * (1.0f + organDrive * 0.25f);
        }

        float outL = orgSum;
        float outR = orgSum;

        // Scanner Vibrato / Chorus (V1-V3 / C1-C3) antes del altavoz rotatorio
        if (vibratoMode > 0 && vibDepthSamples > 0.0f) {
            vibLfoPhase_ += vibInc;
            if (vibLfoPhase_ >= 1.0) vibLfoPhase_ -= 1.0;
            float lfo = static_cast<float>(std::sin(2.0 * PI_F * vibLfoPhase_));
            float delaySamples = 8.0f + vibDepthSamples * (1.0f + lfo);
            float readPos = static_cast<float>(vibWritePos_) - delaySamples;
            if (readPos < 0.0f) readPos += VIB_DELAY_SAMPLES;
            int idx0 = static_cast<int>(readPos) & (VIB_DELAY_SAMPLES - 1);
            int idx1 = (idx0 + 1) & (VIB_DELAY_SAMPLES - 1);
            float frac = readPos - std::floor(readPos);
            float delayed = vibDelayLine_[idx0] * (1.0f - frac) + vibDelayLine_[idx1] * frac;
            outL = useChorus ? (orgSum * 0.55f + delayed * 0.55f) : delayed;
            outR = outL;
        }
        vibDelayLine_[vibWritePos_] = orgSum;
        vibWritePos_ = (vibWritePos_ + 1) & (VIB_DELAY_SAMPLES - 1);

        // Separación de frecuencias del altavoz rotatorio Leslie 122 (Drum < 800Hz, Horn > 800Hz)
        drumLP_state_ += lpCoef * (outL - drumLP_state_);
        float drumSig = drumLP_state_;
        float hornSig = outL - drumSig;

        const double hornAngle = 2.0 * PI_F * horn_.phase;
        const double drumAngle = 2.0 * PI_F * drum_.phase;

        float hornL = 0.65f + 0.35f * static_cast<float>(std::sin(hornAngle));
        float hornR = 0.65f + 0.35f * static_cast<float>(std::cos(hornAngle));
        float drumL = 0.70f + 0.30f * static_cast<float>(std::cos(drumAngle));
        float drumR = 0.70f + 0.30f * static_cast<float>(std::sin(drumAngle));

        float finalL = (hornSig * hornL + drumSig * drumL) * organVolume;
        float finalR = (hornSig * hornR + drumSig * drumR) * organVolume;

        outInterleavedStereo[n * 2]     += finalL;
        outInterleavedStereo[n * 2 + 1] += finalR;
    }
}

} // namespace NordDSP