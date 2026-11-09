#include "NordSynthModule.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace NordDSP {

namespace {
inline uint32_t lcgNext(uint32_t s) { return s * 1664525u + 1013904223u; }
inline float lcgFloat(uint32_t& s) {
    s = lcgNext(s);
    return ((s >> 8) & 0xFFFFFF) / 8388608.0f - 1.0f;
}
}

inline float SingleSynthLayer::polyBLEP(double t, double dt) {
    if (dt <= 0.0 || dt >= 0.5) return 0.0f;
    if (t < dt) {
        t /= dt;
        return static_cast<float>(t + t - t * t - 1.0);
    } else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return static_cast<float>(t + t + t * t + 1.0);
    }
    return 0.0f;
}

inline float SingleSynthLayer::naiveSaw(double phase) {
    return static_cast<float>(2.0 * phase - 1.0);
}

inline float SingleSynthLayer::naiveSquare(double phase) {
    return (phase < 0.5) ? 0.75f : -0.75f;
}

float SingleSynthLayer::readWavetable(int idx, double phase) const {
    if (!wtBank_ || wtBank_->empty()) return 0.0f;
    int safeIdx = std::clamp(idx, 0, static_cast<int>(wtBank_->size()) - 1);
    const auto& wt = (*wtBank_)[safeIdx];
    double p = phase - std::floor(phase);
    int i0 = static_cast<int>(p * SYNTH_WAVETABLE_SIZE) & (SYNTH_WAVETABLE_SIZE - 1);
    int i1 = (i0 + 1) & (SYNTH_WAVETABLE_SIZE - 1);
    float frac = static_cast<float>((p * SYNTH_WAVETABLE_SIZE) - i0);
    return wt.data[i0] * (1.0f - frac) + wt.data[i1] * frac;
}

void SingleSynthLayer::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    filter.reset();
    reset();
}

void SingleSynthLayer::reset() {
    filter.reset();
    for (auto& v : voices) {
        v.active = false;
        v.env = 0.0f;
        v.filtEnv = 0.0f;
        v.stage = 0;
        v.noiseFilterState = 0.0f;
    }
    smoothedCutoff_ = std::clamp(cfg.cutoffHz, 40.0f, 20000.0f);
    smoothedQ_ = std::clamp(cfg.resonanceQ, 0.5f, 12.0f);
}

void SingleSynthLayer::noteOn(int rawKey, int playedKey, int velocity) {
    if (!cfg.enabled) return;

    // Si la misma tecla ya estaba activa en esta capa, pasar sus voces previas a liberación rápida
    for (auto& v : voices) {
        if (v.active && v.rawKey == rawKey) {
            v.keyHeld = false;
            v.sustained = false;
            v.stage = 4;
        }
    }

    float vNorm = std::clamp(velocity, 1, 127) / 127.0f;
    float velSens = std::clamp(cfg.velToAmp, 0.0f, 2.0f);
    float velAmp = (1.0f - 0.65f * velSens * 0.5f) + (vNorm * vNorm) * (0.65f * velSens);
    velAmp = std::clamp(velAmp, 0.15f, 1.5f);

    int unison = std::clamp(cfg.unisonVoices, 1, SYNTH_MAX_UNISON);
    float unisonNorm = 1.0f / std::sqrt(static_cast<float>(unison));

    double keyOffset = (static_cast<double>(playedKey) - 60.0) * static_cast<double>(cfg.keyTrack);
    double pitchNote = 60.0 + keyOffset + cfg.octave * 12.0 + cfg.semitones + (cfg.detuneCents / 100.0);
    double baseFreq = 440.0 * std::pow(2.0, (pitchNote - 69.0) / 12.0);

    for (int u = 0; u < unison; ++u) {
        SynthPolyVoice* target = nullptr;
        for (auto& v : voices) {
            if (!v.active) { target = &v; break; }
        }
        if (!target) {
            // Robar primero voces en etapa Release (stage == 4) o la de menor amplitud
            float minScore = 1e9f;
            for (auto& v : voices) {
                float score = v.env + (v.stage == 4 ? 0.0f : 1.0f);
                if (score < minScore) {
                    minScore = score;
                    target = &v;
                }
            }
            if (!target) target = &voices[0];
        }

        target->active = true;
        target->rawKey = rawKey;
        target->playedKey = playedKey;
        target->keyHeld = true;
        target->sustained = false;
        target->stage = 1;
        target->env = 0.0f;
        target->filtEnv = 0.0f;
        target->velGain = velAmp;
        target->velNorm = vNorm;
        target->unisonScale = unisonNorm;
        target->noiseFilterState = 0.0f;
        target->freq = baseFreq;

        if (unison > 1) {
            float t = static_cast<float>(u) / static_cast<float>(unison - 1) - 0.5f;
            float detCents = cfg.unisonDetune * t * 2.0f;
            target->detuneMult = static_cast<float>(std::pow(2.0, detCents / 1200.0));
            target->panOffset = t * cfg.unisonSpread * 1.6f;
        } else {
            target->detuneMult = 1.0f;
            target->panOffset = 0.0f;
        }

        uint32_t seed = static_cast<uint32_t>(rawKey) * 2654435761u
                      + static_cast<uint32_t>(velocity) * 2246822519u
                      + static_cast<uint32_t>(u) * 1013904223u + 0x9E3779B9u;
        double jitter = (unison > 1) ? (((lcgNext(seed) & 0xFFFF) / 65536.0) * 0.35) : 0.0;
        double basePhase = cfg.phaseOffset + jitter;
        target->phase1 = basePhase - std::floor(basePhase);
        target->phase2 = (lcgNext(seed) & 0xFFFF) / 65536.0;
        target->phase3 = (lcgNext(seed) & 0xFFFF) / 65536.0;
    }
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

void SingleSynthLayer::renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr,
                                   float macroCutoff, const std::vector<Wavetable>& wtBank,
                                   const SynthLFO* lfos, const SynthEnvelope* /*envs*/) {
    if (!cfg.enabled || cfg.volume <= 0.001f) return;
    sr_ = sr;
    wtBank_ = &wtBank;

    float targetCutoff = std::clamp(cfg.cutoffHz * macroCutoff, 40.0f, 20000.0f);
    float targetQ = std::clamp(cfg.resonanceQ, 0.5f, 12.0f);
    float smoothCoef = std::exp(-1.0f / (0.003f * sr));

    const float atkSec = std::max(0.001f, cfg.attackSec);
    const float decSec = std::max(0.005f, cfg.decaySec);
    const float relSec = std::max(0.005f, cfg.releaseSec);
    const float atkCoef = 1.0f - std::exp(-1.0f / (atkSec * sr));
    const float decCoef = std::exp(-5.0f / (decSec * sr));
    const float relCoef = std::exp(-6.0f / (relSec * sr));
    const float susLvl = std::clamp(cfg.sustainLevel, 0.0f, 1.0f);

    const float fAtkSec = std::max(0.001f, cfg.filtAttackSec);
    const float fDecSec = std::max(0.005f, cfg.filtDecaySec);
    const float fRelSec = std::max(0.005f, cfg.filtReleaseSec);
    const float fAtkCoef = 1.0f - std::exp(-1.0f / (fAtkSec * sr));
    const float fDecCoef = std::exp(-5.0f / (fDecSec * sr));
    const float fRelCoef = std::exp(-6.0f / (fRelSec * sr));
    const float fSusLvl = std::clamp(cfg.filtSustainLvl, 0.0f, 1.0f);

    const float layerGain = cfg.volume * 0.25f;

    // Modulación de Pitch por LFO
    float lfoPitchRatio = 1.0f;
    if (lfos && lfos[0].enabled && lfos[0].destPitch != 0) {
        lfoPitchRatio = std::pow(2.0f, (lfos[0].currentVal * lfos[0].destPitch) / 1200.0f);
    }

    for (unsigned int n = 0; n < numFrames; ++n) {
        smoothedCutoff_ = targetCutoff + smoothCoef * (smoothedCutoff_ - targetCutoff);
        smoothedQ_ = targetQ + smoothCoef * (smoothedQ_ - targetQ);

        float sumL = 0.0f, sumR = 0.0f;
        float avgFiltEnv = 0.0f;
        float avgVel = 0.0f;
        int activeVoiceCnt = 0;

        for (auto& v : voices) {
            if (!v.active) continue;

            switch (v.stage) {
                case 1:
                    v.env += (1.0f - v.env) * atkCoef;
                    v.filtEnv += (1.0f - v.filtEnv) * fAtkCoef;
                    if (v.env >= 0.998f) { v.env = 1.0f; v.stage = 2; }
                    break;
                case 2:
                    v.env = susLvl + (v.env - susLvl) * decCoef;
                    v.filtEnv = fSusLvl + (v.filtEnv - fSusLvl) * fDecCoef;
                    if (std::abs(v.env - susLvl) < 1e-4f) { v.env = susLvl; v.stage = 3; }
                    break;
                case 3:
                    v.env = susLvl;
                    v.filtEnv = fSusLvl + (v.filtEnv - fSusLvl) * fDecCoef;
                    break;
                case 4:
                    v.env *= relCoef;
                    v.filtEnv *= fRelCoef;
                    if (v.env < 1e-4f) { v.env = 0.0f; v.active = false; }
                    break;
            }
            if (!v.active) continue;

            avgFiltEnv += v.filtEnv;
            avgVel += v.velNorm;
            activeVoiceCnt++;

            double inc = (v.freq / sr) * v.detuneMult * lfoPitchRatio;
            float osc = 0.0f;

            switch (cfg.waveType) {
                case 0: // Sawtooth con PolyBLEP
                    osc = naiveSaw(v.phase1) - polyBLEP(v.phase1, inc);
                    break;
                case 1: // Square con PolyBLEP
                    osc = naiveSquare(v.phase1) + polyBLEP(v.phase1, inc) - polyBLEP(std::fmod(v.phase1 + 0.5, 1.0), inc);
                    osc *= 0.85f;
                    break;
                case 2: // Triangle limpio libre de DC y sin estado compartido
                    osc = static_cast<float>(1.0 - 4.0 * std::abs(v.phase1 - 0.5));
                    break;
                case 3: // Sine
                    osc = std::sin(2.0f * PI_F * static_cast<float>(v.phase1));
                    break;
                case 4: { // SuperSaw (3 osciladores con PolyBLEP)
                    float s1 = naiveSaw(v.phase1) - polyBLEP(v.phase1, inc);
                    float s2 = naiveSaw(v.phase2) - polyBLEP(v.phase2, inc * 1.0065);
                    float s3 = naiveSaw(v.phase3) - polyBLEP(v.phase3, inc * 0.9935);
                    osc = (s1 + s2 * 0.82f + s3 * 0.82f) * 0.44f;
                    v.phase2 += inc * 1.0065; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                    v.phase3 += inc * 0.9935; if (v.phase3 >= 1.0) v.phase3 -= 1.0;
                    break;
                }
                case 5: { // Analog Warm Noise (con estado individual por voz)
                    uint32_t seed = static_cast<uint32_t>(v.rawKey) * 2654435761u + n * 1664525u;
                    float white = lcgFloat(seed);
                    v.noiseFilterState += 0.35f * (white - v.noiseFilterState);
                    osc = v.noiseFilterState * 0.6f;
                    break;
                }
                case 6: // Wavetable
                    osc = readWavetable(cfg.wavetableIdx, v.phase1);
                    break;
                default:
                    osc = std::sin(2.0f * PI_F * static_cast<float>(v.phase1));
                    break;
            }

            v.phase1 += inc;
            if (v.phase1 >= 1.0) v.phase1 -= 1.0;

            float amp = osc * v.env * v.velGain * v.unisonScale;
            float voicePan = std::clamp(cfg.pan + v.panOffset, -1.0f, 1.0f);
            float pAng = (voicePan + 1.0f) * (PI_F * 0.25f);
            sumL += amp * std::cos(pAng);
            sumR += amp * std::sin(pAng);
        }

        if (activeVoiceCnt == 0) continue;

        float invCnt = 1.0f / static_cast<float>(activeVoiceCnt);
        avgFiltEnv *= invCnt;
        avgVel *= invCnt;

        float filtModCutoff = smoothedCutoff_;
        if (std::abs(cfg.filterEnvAmt) > 0.001f) {
            filtModCutoff *= std::pow(2.0f, cfg.filterEnvAmt * 4.5f * avgFiltEnv);
        }
        if (cfg.velToCutoff > 0.001f) {
            filtModCutoff *= (1.0f + cfg.velToCutoff * avgVel * 2.0f);
        }
        if (lfos) {
            for (int li = 0; li < SYNTH_NUM_LFOS; ++li) {
                if (lfos[li].enabled && lfos[li].destCutoff != 0) {
                    filtModCutoff += lfos[li].currentVal * lfos[li].destCutoff;
                }
            }
        }
        filtModCutoff = std::clamp(filtModCutoff, 40.0f, 20000.0f);
        filter.setParams(sr, filtModCutoff, smoothedQ_);

        float fL = sumL * layerGain;
        float fR = sumR * layerGain;
        filter.processStereo(fL, fR);

        mixL[n] += fL;
        mixR[n] += fR;
    }
}

void NordSynthModule::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    for (auto& l : layers) l.init(sr_);
    bufL_.assign(4096, 0.0f);
    bufR_.assign(4096, 0.0f);
    generateDefaultWavetables();
    loadBuiltInSynthPreset(synthPresetId);
}

void NordSynthModule::generateDefaultWavetables() {
    std::lock_guard<std::mutex> lock(wtMutex);
    wavetableBank.clear();

    Wavetable sine; sine.name = "Sine";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i)
        sine.data[i] = std::sin(2.0f * PI_F * i / SYNTH_WAVETABLE_SIZE);
    wavetableBank.push_back(sine);

    Wavetable tri; tri.name = "Triangle";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i) {
        float p = static_cast<float>(i) / SYNTH_WAVETABLE_SIZE;
        tri.data[i] = (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    }
    wavetableBank.push_back(tri);

    Wavetable sqr; sqr.name = "Square";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i) {
        float v = 0.0f;
        for (int k = 1; k <= 15; k += 2)
            v += std::sin(2.0f * PI_F * k * i / SYNTH_WAVETABLE_SIZE) / k;
        sqr.data[i] = v * 0.8f;
    }
    wavetableBank.push_back(sqr);

    Wavetable saw; saw.name = "Saw";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i) {
        float v = 0.0f;
        for (int k = 1; k <= 20; ++k)
            v += std::sin(2.0f * PI_F * k * i / SYNTH_WAVETABLE_SIZE) * ((k & 1) ? 1.0f : -1.0f) / k;
        saw.data[i] = v * 0.6f;
    }
    wavetableBank.push_back(saw);

    Wavetable organ; organ.name = "Organ";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i) {
        float v = 0.0f;
        for (int k = 1; k <= 9; k += 2)
            v += std::sin(2.0f * PI_F * k * i / SYNTH_WAVETABLE_SIZE) / k;
        organ.data[i] = v * 0.7f;
    }
    wavetableBank.push_back(organ);

    Wavetable bell; bell.name = "Bell";
    for (int i = 0; i < SYNTH_WAVETABLE_SIZE; ++i) {
        float p = static_cast<float>(i) / SYNTH_WAVETABLE_SIZE * 2.0f * PI_F;
        bell.data[i] = std::sin(p) * std::sin(p * 3.0f) * 0.85f;
    }
    wavetableBank.push_back(bell);
}

void NordSynthModule::addWavetable(const Wavetable& wt) {
    std::lock_guard<std::mutex> lock(wtMutex);
    wavetableBank.push_back(wt);
}

void NordSynthModule::clearWavetables() {
    std::lock_guard<std::mutex> lock(wtMutex);
    wavetableBank.clear();
}

void NordSynthModule::setLFO(int idx, const SynthLFO& lfo) {
    if (idx >= 0 && idx < SYNTH_NUM_LFOS) lfos[idx] = lfo;
}

void NordSynthModule::setEnvelope(int idx, const SynthEnvelope& env) {
    if (idx >= 0 && idx < SYNTH_NUM_ENVS) envelopes[idx] = env;
}

void NordSynthModule::updateLFOsAndEnvs(unsigned int numFrames, float sr) {
    for (auto& lfo : lfos) {
        if (!lfo.enabled) { lfo.currentVal = 0.0f; continue; }
        double inc = lfo.rateHz / sr;
        lfo.phaseAccum += inc * numFrames;
        if (lfo.phaseAccum >= 1.0) lfo.phaseAccum -= std::floor(lfo.phaseAccum);
        float val = std::sin(2.0f * PI_F * static_cast<float>(lfo.phaseAccum));
        lfo.currentVal = val * lfo.depth;
    }
}

void NordSynthModule::loadBuiltInSynthPreset(int presetId) {
    synthPresetId = std::clamp(presetId, 1, 8);
    for (auto& l : layers) l.reset();

    if (synthPresetId == 1) { // 1: Warm Pad
        layers[0].cfg = {true, 4, 0.80f, -0.20f, 0, 0, -6.0f, 4800.0f, 0.8f, 0.18f, 2.2f, 0.85f, 0.95f,
                         3, 10.0f, 0.5f, 0.0f, 1.0f, 0.2f, 1.0f, 0, "", 0, 0.15f, 0.5f, 0.12f, 1.5f, 0.7f, 1.2f};
        layers[1].cfg = {true, 0, 0.65f, 0.20f, -1, 0, 6.0f, 3600.0f, 0.9f, 0.22f, 2.5f, 0.80f, 1.10f,
                         2, 12.0f, 0.6f, 0.0f, 1.0f, 0.2f, 1.0f, 0, "", 0, 0.15f, 0.5f, 0.15f, 1.5f, 0.7f, 1.2f};
        layers[2].cfg = {true, 3, 0.55f, 0.00f, +1, 0, 0.0f, 9000.0f, 0.7f, 0.35f, 2.8f, 0.90f, 1.30f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0, "", 0, 0.0f, 0.5f, 0.10f, 1.5f, 0.7f, 1.2f};
    } else if (synthPresetId == 2) { // 2: 80s Brass
        layers[0].cfg = {true, 0, 0.85f, -0.25f, 0, 0, -9.0f, 3800.0f, 1.3f, 0.025f, 0.9f, 0.70f, 0.35f,
                         2, 14.0f, 0.6f, 0.0f, 1.0f, 0.45f, 1.0f, 0, "", 0, 0.55f, 0.5f, 0.03f, 0.45f, 0.4f, 0.35f};
        layers[1].cfg = {true, 0, 0.85f, 0.25f, 0, 0, 9.0f, 3800.0f, 1.3f, 0.035f, 0.9f, 0.70f, 0.35f,
                         2, 14.0f, 0.6f, 0.0f, 1.0f, 0.45f, 1.0f, 0, "", 0, 0.55f, 0.5f, 0.04f, 0.45f, 0.4f, 0.35f};
        layers[2].cfg = {true, 1, 0.50f, 0.00f, -1, 0, 0.0f, 4200.0f, 1.0f, 0.020f, 1.2f, 0.65f, 0.40f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 1.0f, 0, "", 0, 0.30f, 0.5f, 0.02f, 0.5f, 0.5f, 0.4f};
    } else if (synthPresetId == 3) { // 3: SuperSaw Lead
        layers[0].cfg = {true, 4, 0.90f, -0.30f, 0, 0, -12.0f, 13000.0f, 1.1f, 0.005f, 1.5f, 0.90f, 0.50f,
                         4, 22.0f, 0.8f, 0.0f, 1.0f, 0.3f, 1.0f, 0, "", 0, 0.2f, 0.5f, 0.005f, 0.4f, 0.8f, 0.4f};
        layers[1].cfg = {true, 4, 0.85f, 0.30f, +1, 0, 12.0f, 11500.0f, 1.1f, 0.008f, 1.5f, 0.85f, 0.55f,
                         4, 22.0f, 0.8f, 0.0f, 1.0f, 0.3f, 1.0f, 0, "", 0, 0.2f, 0.5f, 0.005f, 0.4f, 0.8f, 0.4f};
        layers[2].cfg = {true, 1, 0.65f, 0.00f, -1, 0, 0.0f, 5500.0f, 0.9f, 0.005f, 1.8f, 0.80f, 0.45f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0, "", 0, 0.0f, 0.5f, 0.01f, 0.5f, 0.5f, 0.5f};
    } else if (synthPresetId == 4) { // 4: Crystal Pluck
        layers[0].cfg = {true, 6, 0.85f, -0.15f, +1, 0, -4.0f, 9500.0f, 1.4f, 0.002f, 0.45f, 0.15f, 0.65f,
                         2, 8.0f, 0.4f, 0.0f, 1.0f, 0.4f, 1.0f, 5, "", 0, 0.5f, 0.5f, 0.002f, 0.25f, 0.1f, 0.35f};
        layers[1].cfg = {true, 3, 0.75f, 0.15f, +2, 7, 4.0f, 14000.0f, 0.8f, 0.002f, 0.35f, 0.10f, 0.75f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 1.0f, 0, "", 0, 0.0f, 0.5f, 0.01f, 0.3f, 0.2f, 0.3f};
        layers[2].cfg = {false, 0, 0.50f, 0.00f, 0, 0, 0.0f, 8000.0f, 0.7f, 0.010f, 1.00f, 0.50f, 0.40f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0, "", 0, 0.0f, 0.5f, 0.01f, 0.5f, 0.5f, 0.5f};
    } else { // 5..8: Analog Strings
        layers[0].cfg = {true, 4, 0.78f, -0.25f, 0, 0, -10.0f, 6800.0f, 0.8f, 0.28f, 2.0f, 0.90f, 1.20f,
                         4, 16.0f, 0.7f, 0.0f, 1.0f, 0.15f, 1.0f, 0, "", 0, 0.1f, 0.5f, 0.25f, 1.5f, 0.8f, 1.2f};
        layers[1].cfg = {true, 2, 0.70f, 0.25f, 0, 0, 10.0f, 5400.0f, 0.8f, 0.32f, 2.2f, 0.85f, 1.25f,
                         3, 14.0f, 0.6f, 0.0f, 1.0f, 0.15f, 1.0f, 0, "", 0, 0.1f, 0.5f, 0.28f, 1.5f, 0.8f, 1.2f};
        layers[2].cfg = {true, 3, 0.60f, 0.00f, -1, 0, 0.0f, 3200.0f, 0.7f, 0.20f, 2.5f, 0.95f, 1.10f,
                         1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0, "", 0, 0.0f, 0.5f, 0.15f, 1.0f, 0.7f, 1.0f};
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
    if (bufL_.size() < numFrames) {
        bufL_.resize(numFrames);
        bufR_.resize(numFrames);
    }
    std::fill(bufL_.begin(), bufL_.begin() + numFrames, 0.0f);
    std::fill(bufR_.begin(), bufR_.begin() + numFrames, 0.0f);

    updateLFOsAndEnvs(numFrames, sr_);

    for (auto& l : layers) {
        l.renderLayer(bufL_.data(), bufR_.data(), numFrames, sr_,
                      synthMacroCutoff, wavetableBank, lfos.data(), envelopes.data());
    }
    for (unsigned int n = 0; n < numFrames; ++n) {
        outInterleavedStereo[n * 2]     += bufL_[n] * synthMasterVol;
        outInterleavedStereo[n * 2 + 1] += bufR_[n] * synthMasterVol;
    }
}

} // namespace NordDSP