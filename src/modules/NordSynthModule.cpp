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
    pwmPhase_ = 0.0;
    lastNoteFrequency_ = 0.0;
    for (auto& v : voices) {
        v.active = false;
        v.env = 0.0f;
        v.filtEnv = 0.0f;
        v.stage = 0;
        v.noiseFilterState = 0.0f;
        v.filterIc1 = v.filterIc2 = v.highpassState = 0.0f;
        v.filterA1 = 1.0f; v.filterA2 = v.filterA3 = 0.0f;
        v.cutoffSmooth = std::clamp(cfg.cutoffHz, 40.0f, 18000.0f);
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
    if (voiceMode != 0) {
        bool reused = false;
        for (auto& v : voices) {
            if (!v.active || !v.keyHeld) continue;
            v.rawKey=rawKey;v.playedKey=playedKey;v.targetFreq=baseFreq;
            v.slidePitch=slideSemis;
            v.velNorm=vNorm;v.velGain=velAmp;
            if (voiceMode == 1) { v.stage=1; /* attack from current level, no discontinuity */ }
            reused=true;
        }
        if (reused) {lastNoteFrequency_=baseFreq;return;}
    }
    const double startFreq = (glideSeconds>0.001f && glideMode==0 && lastNoteFrequency_>0.0)
                                ? lastNoteFrequency_ : baseFreq;
    lastNoteFrequency_=baseFreq;

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
        target->noiseSeed = (static_cast<uint32_t>(rawKey + 1) * 1664525u) ^
                            (static_cast<uint32_t>(velocity + 1) * 1013904223u) ^ (u + 17u);
        target->filterIc1 = target->filterIc2 = target->highpassState = 0.0f;
        target->filterA1 = 1.0f; target->filterA2 = target->filterA3 = 0.0f;
        target->cutoffSmooth = std::clamp(cfg.cutoffHz, 40.0f, 18000.0f);
        target->freq = startFreq;
        target->targetFreq = baseFreq;
        target->slidePitch = slideSemis;

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
        target->phaseSub = target->phase1;
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
    wtBank_ = &wtBank;
    const float smooth = std::exp(-16.0f / (0.008f * sr));
    const float atkCoef = 1.0f - std::exp(-1.0f / (std::max(0.001f, cfg.attackSec) * sr));
    const float decCoef = std::exp(-5.0f / (std::max(0.005f, cfg.decaySec) * sr));
    const float relCoef = std::exp(-6.0f / (std::max(0.005f, cfg.releaseSec) * sr));
    const float sus = std::clamp(cfg.sustainLevel, 0.0f, 1.0f);
    const float fAtk = 1.0f - std::exp(-1.0f / (std::max(0.001f, cfg.filtAttackSec) * sr));
    const float fDec = std::exp(-5.0f / (std::max(0.005f, cfg.filtDecaySec) * sr));
    const float fRel = std::exp(-6.0f / (std::max(0.005f, cfg.filtReleaseSec) * sr));
    const float fSus = std::clamp(cfg.filtSustainLvl, 0.0f, 1.0f);
    const float mainCutoff = std::clamp(cfg.cutoffHz * macroCutoff, 40.0f, 20000.0f);
    const float resonanceK = 1.0f / std::clamp(cfg.resonanceQ, 0.5f, 12.0f);
    const float hpCut = std::clamp(cfg.highpassHz, 20.0f, sr * 0.40f);
    const float hpAlpha = std::exp(-2.0f * PI_F * hpCut / sr);
    const float osc2Ratio = std::pow(2.0f, (cfg.osc2Semitones + cfg.osc2Detune / 100.0f) / 12.0f);
    const float layerGain = cfg.volume * 0.20f;
    const float basePW = std::clamp(cfg.pulseWidth, 0.05f, 0.95f);
    const float pwmDepth = std::clamp(cfg.pwmDepth, 0.0f, 0.45f);
    const float sawLevel = std::clamp(cfg.sawLevel, 0.0f, 1.0f);
    const float pulseLevel = std::clamp(cfg.pulseLevel, 0.0f, 1.0f);
    const float subLevel = std::clamp(cfg.subLevel, 0.0f, 1.0f);
    const float noiseLevel = std::clamp(cfg.noiseLevel, 0.0f, 1.0f);
    const float osc2Level = std::clamp(cfg.osc2Level, 0.0f, 1.0f);
    const float analogNorm = 1.0f / std::max(1.0f, sawLevel + pulseLevel + subLevel + noiseLevel + osc2Level);
    const float driveAmount = std::clamp(cfg.drive, 0.0f, 1.0f);
    const float pitchLfo = (lfos && lfos[0].enabled) ?
        std::pow(2.0f, lfos[0].currentVal * lfos[0].destPitch / 1200.0f) : 1.0f;
    const float filterLfo = (lfos && lfos[0].enabled) ?
        lfos[0].currentVal * lfos[0].destCutoff : 0.0f;
    const float maxCutoff = std::min(20000.0f, sr * 0.44f);
    // Calculate stereo panning once per render block, not for every voice sample.
    for (auto& v : voices) {
        if (!v.active) continue;
        const float pan = std::clamp(cfg.pan + v.panOffset, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * (PI_F * 0.25f);
        v.panGainL = std::cos(angle);
        v.panGainR = std::sin(angle);
    }

    const float bendRatio = std::pow(2.f, performancePitchBendSemis / 12.f);
    const float glideAlpha = glideSeconds < 0.001f ? 1.0f :
        1.0f-std::exp(-1.0f/(std::max(.001f,glideSeconds)*sr));
    const float slideDecay = std::exp(-1.0f/(.15f*sr));
    for (unsigned int n = 0; n < numFrames; ++n) {
        pwmPhase_ += std::clamp(cfg.pwmRateHz, 0.05f, 12.0f) / sr;
        if (pwmPhase_ >= 1.0) pwmPhase_ -= 1.0;
        const float pw = std::clamp(basePW + pwmDepth *
            std::sin(2.0f * PI_F * static_cast<float>(pwmPhase_)), 0.05f, 0.95f);
        float sumL = 0.0f, sumR = 0.0f;

        for (auto& v : voices) {
            if (!v.active) continue;
            switch (v.stage) {
                case 1:
                    v.env += (1.0f - v.env) * atkCoef;
                    v.filtEnv += (1.0f - v.filtEnv) * fAtk;
                    if (v.env >= 0.998f) { v.env = 1.0f; v.stage = 2; }
                    break;
                case 2:
                    v.env = sus + (v.env - sus) * decCoef;
                    v.filtEnv = fSus + (v.filtEnv - fSus) * fDec;
                    if (std::abs(v.env - sus) < 1e-4f) { v.env = sus; v.stage = 3; }
                    break;
                case 3:
                    v.env = sus;
                    v.filtEnv = fSus + (v.filtEnv - fSus) * fDec;
                    break;
                case 4:
                    v.env *= relCoef;
                    v.filtEnv *= fRel;
                    if (v.env < 1e-4f) { v.env = 0.0f; v.active = false; }
                    break;
                default: break;
            }
            if (!v.active) continue;

            v.freq += (v.targetFreq - v.freq) * glideAlpha;
            v.slidePitch *= slideDecay;
            const float vibCents = performanceModWheel * 30.f *
                std::sin(2.f*PI_F*static_cast<float>(pwmPhase_) * (5.2f / std::max(0.05f,cfg.pwmRateHz)));
            const double performanceRatio = bendRatio * std::pow(2.f,(vibCents/100.f+v.slidePitch)/12.f);
            const double inc = std::clamp(v.freq * v.detuneMult * pitchLfo * performanceRatio / sr,
                                          0.0000001, 0.45);
            const double inc2 = std::clamp(inc * osc2Ratio, 0.0000001, 0.45);
            auto saw = [this](double phase, double step) -> float {
                return naiveSaw(phase) - polyBLEP(phase, step);
            };
            auto pulse = [this](double phase, double step, float width) -> float {
                // Both discontinuities are band-limited. Center around zero for arbitrary duty.
                const double shifted = std::fmod(phase + 1.0 - width, 1.0);
                return (phase < width ? 1.0f : -1.0f) +
                    polyBLEP(phase, step) - polyBLEP(shifted, step) - (2.0f * width - 1.0f);
            };
            float osc = 0.0f;
            switch (cfg.waveType) {
                case 0: osc = saw(v.phase1, inc); break;
                case 1: osc = pulse(v.phase1, inc, pw) * 0.85f; break;
                case 2: osc = static_cast<float>(1.0 - 4.0 * std::abs(v.phase1 - 0.5)); break;
                case 3: osc = std::sin(2.0f * PI_F * static_cast<float>(v.phase1)); break;
                case 4: {
                    const float s1 = saw(v.phase1, inc);
                    const float s2 = saw(v.phase2, std::min(0.45, inc * 1.0065));
                    const float s3 = saw(v.phase3, inc * 0.9935);
                    osc = (s1 + 0.82f * (s2 + s3)) * 0.44f;
                    v.phase2 += inc * 1.0065;
                    v.phase3 += inc * 0.9935;
                    v.phase2 -= std::floor(v.phase2);
                    v.phase3 -= std::floor(v.phase3);
                    break;
                }
                case 5:
                    osc = lcgFloat(v.noiseSeed) * 0.55f;
                    break;
                case 6:
                    osc = readWavetable(cfg.wavetableIdx, v.phase1);
                    break;
                case 7: { // hard sync: reset slave phase on master wrap
                    const double slaveInc = std::min(.45, inc * (2.0 + 0.5*osc2Ratio));
                    osc = saw(v.phase2, slaveInc);
                    v.phase2 += slaveInc;
                    if (v.phase1 + inc >= 1.0) v.phase2 = 0.0;
                    v.phase2 -= std::floor(v.phase2);
                    break;
                }
                case 10: { // two-operator FM with controlled index
                    const float carrier = static_cast<float>(2.0 * PI_F * v.phase1);
                    const float modulator = std::sin(2.0f*PI_F*static_cast<float>(v.phase2));
                    osc = std::sin(carrier + (1.0f + osc2Level*4.0f)*modulator) * .78f;
                    v.phase2 += std::min(.45, inc * std::max(1.0f,osc2Ratio*2.0f));
                    v.phase2 -= std::floor(v.phase2);
                    break;
                }
                case 11: { // antialiased triple pulse
                    osc = (pulse(v.phase1,inc,pw) +
                           .78f*pulse(v.phase2,std::min(.45,inc*1.008),pw) +
                           .78f*pulse(v.phase3,inc*.992,pw)) * .39f;
                    v.phase2=std::fmod(v.phase2+inc*1.008,1.0);
                    v.phase3=std::fmod(v.phase3+inc*.992,1.0);
                    break;
                }
                case 12: { // ring modulation
                    osc = saw(v.phase1,inc) * std::sin(2.0f*PI_F*static_cast<float>(v.phase2));
                    v.phase2=std::fmod(v.phase2+std::min(.45,inc*osc2Ratio*1.5),1.0);
                    break;
                }
                case 13: { // vocal-formant additive oscillator, bounded harmonics
                    const float p=2.0f*PI_F*static_cast<float>(v.phase1);
                    osc = (.35f*std::sin(p)+.45f*std::sin(2*p)+.26f*std::sin(3*p)
                           +.15f*std::sin(5*p)+.10f*std::sin(7*p))*.72f;
                    break;
                }
                case 14: { // wavefolder with smooth nonlinear folding
                    const float x=1.0f+2.0f*osc2Level;
                    osc = std::sin(x*2.7f*std::sin(2.0f*PI_F*static_cast<float>(v.phase1)))*.75f;
                    break;
                }
                case 8: { // Juno style: simultaneously mix saw, PWM pulse, sub and noise
                    const float sub = pulse(v.phaseSub, inc * 0.5, 0.50f);
                    osc = analogNorm * (sawLevel * saw(v.phase1, inc) +
                        pulseLevel * pulse(v.phase1, inc, pw) +
                        subLevel * sub + noiseLevel * lcgFloat(v.noiseSeed));
                    break;
                }
                case 9: { // Jupiter style: same DCO + independently tunable second oscillator
                    float oscB = saw(v.phase2, inc2);
                    if (cfg.osc2Wave == 1) oscB = pulse(v.phase2, inc2, pw);
                    if (cfg.osc2Wave == 2)
                        oscB = static_cast<float>(1.0 - 4.0 * std::abs(v.phase2 - 0.5));
                    osc = analogNorm * (sawLevel * saw(v.phase1, inc) +
                        pulseLevel * pulse(v.phase1, inc, pw) +
                        subLevel * pulse(v.phaseSub, inc * 0.5, 0.50f) +
                        noiseLevel * lcgFloat(v.noiseSeed) + osc2Level * oscB);
                    v.phase2 += inc2;
                    v.phase2 -= std::floor(v.phase2);
                    break;
                }
                default: osc = std::sin(2.0f * PI_F * static_cast<float>(v.phase1)); break;
            }
            v.phase1 += inc;
            v.phase1 -= std::floor(v.phase1);
            v.phaseSub += inc * 0.5;
            v.phaseSub -= std::floor(v.phaseSub);

            // Per-note filter: recalculate expensive frequency coefficients at 1/16th
            // audio rate; run the actual state-variable filter at full sample rate.
            // This is crucial when playing multiple notes across all three layers live.
            if ((n & 15u) == 0u) {
                const float keyRatio = std::exp2((v.playedKey - 60) *
                    (cfg.filterKeyTrack / 12.0f));
                const float envRatio = std::exp2(std::clamp(cfg.filterEnvAmt,
                    -1.0f, 1.0f) * 5.0f * v.filtEnv);
                const float velRatio = 1.0f + cfg.velToCutoff * v.velNorm * 2.0f;
                const float cutoff = std::clamp((mainCutoff + filterLfo) *
                    envRatio * velRatio * keyRatio, 30.0f, maxCutoff);
                v.cutoffSmooth = std::clamp(cutoff + smooth * (v.cutoffSmooth - cutoff), 30.0f, maxCutoff);
                const float g = std::tan(PI_F * v.cutoffSmooth / sr);
                v.filterA1 = 1.0f / (1.0f + g * (g + resonanceK));
                v.filterA2 = g * v.filterA1;
                v.filterA3 = g * v.filterA2;
            }

            // One-pole high-pass (pre-VCF) + topology-preserving state-variable filter.
            v.highpassState = (1.0f - hpAlpha) * osc + hpAlpha * v.highpassState;
            float input = osc - v.highpassState;
            if (driveAmount > 0.001f) {
                const float gain = 1.0f + driveAmount * 3.0f;
                input = std::tanh(input * gain) / std::tanh(gain);
            }
            const float a1 = v.filterA1;
            const float a2 = v.filterA2;
            const float a3 = v.filterA3;
            const float v3 = input - v.filterIc2;
            const float bp = a1 * v.filterIc1 + a2 * v3;
            const float lp = v.filterIc2 + a2 * v.filterIc1 + a3 * v3;
            v.filterIc1 = 2.0f * bp - v.filterIc1;
            v.filterIc2 = 2.0f * lp - v.filterIc2;
            float filtered = lp;
            if (cfg.filterType == 1) filtered = input - resonanceK * bp - lp;
            if (cfg.filterType == 2) filtered = bp;
            if (cfg.filterType == 3) filtered = input - resonanceK * bp; // true notch = LP + HP
            const float amp = filtered * v.env * v.velGain * v.unisonScale * layerGain;
            sumL += amp * v.panGainL;
            sumR += amp * v.panGainR;
        }
        mixL[n] += sumL;
        mixR[n] += sumR;
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
        lfo.phaseAccum += (std::clamp(lfo.rateHz, 0.05f, 30.0f) / sr) * numFrames;
        lfo.phaseAccum -= std::floor(lfo.phaseAccum);
        const float p = static_cast<float>(lfo.phaseAccum);
        float val = std::sin(2.0f * PI_F * p);
        if (lfo.waveType == 1) val = 1.0f - 4.0f * std::abs(p - 0.5f);
        if (lfo.waveType == 2) val = 2.0f * p - 1.0f;
        if (lfo.waveType == 3) val = p < 0.5f ? 1.0f : -1.0f;
        lfo.currentVal = val * std::clamp(lfo.depth, 0.0f, 1.0f);
    }
}

void NordSynthModule::loadBuiltInSynthPreset(int presetId) {
    synthPresetId = std::clamp(presetId, 1, 120);

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

    // Analog-inspired factory bank. Three independent layers are always retained.
    // Factory sounds are interpretations, not circuit/component emulations.
    if (synthPresetId == 1) { // Juno Warm Pad
        layers[0].cfg.waveType = 8; layers[0].cfg.pwmDepth = 0.18f;
        layers[0].cfg.pwmRateHz = 0.24f; layers[0].cfg.subLevel = 0.17f;
        layers[1].cfg.waveType = 8; layers[1].cfg.pwmDepth = 0.24f;
        layers[1].cfg.pwmRateHz = 0.31f; layers[1].cfg.sawLevel = 0.40f;
    } else if (synthPresetId == 2) { // Jupiter Brass
        for (auto& l : layers) { l.cfg.waveType = 9; l.cfg.osc2Level = 0.70f;
            l.cfg.osc2Detune = 8.0f; l.cfg.filterEnvAmt = 0.75f;
            l.cfg.filtAttackSec = 0.01f; l.cfg.filtDecaySec = 0.30f;
            l.cfg.filtSustainLvl = 0.20f; l.cfg.sawLevel = 0.85f; }
    } else if (synthPresetId == 3) { // Jupiter Unison Lead
        layers[0].cfg.waveType = 9; layers[0].cfg.osc2Level = 0.90f;
        layers[0].cfg.osc2Detune = 13.0f;
        layers[1].cfg.waveType = 9; layers[1].cfg.osc2Semitones = 12;
        layers[1].cfg.osc2Level = 0.25f;
    } else if (synthPresetId == 4) { // Crystal Pluck; legacy wavetable sound preserved
        layers[0].cfg.filterEnvAmt = 0.45f;
        layers[1].cfg.filterEnvAmt = 0.30f;
    } else if (synthPresetId == 5) { // Juno Strings
        layers[0].cfg.waveType = 8; layers[0].cfg.pulseLevel = 0.80f;
        layers[0].cfg.pwmDepth = 0.28f; layers[0].cfg.pwmRateHz = 0.18f;
        layers[1].cfg.waveType = 8; layers[1].cfg.pwmDepth = 0.30f;
        layers[1].cfg.pwmRateHz = 0.26f;
    } else if (synthPresetId == 6) { // Deep Analog Bass
        layers[0].cfg = SynthLayerConfig{};
        layers[0].cfg.enabled = true; layers[0].cfg.waveType = 8;
        layers[0].cfg.volume = 0.90f; layers[0].cfg.cutoffHz = 800.0f;
        layers[0].cfg.filterEnvAmt = 0.75f; layers[0].cfg.resonanceQ = 1.7f;
        layers[0].cfg.filtDecaySec = 0.27f; layers[0].cfg.filtSustainLvl = 0.06f;
        layers[0].cfg.attackSec = 0.002f; layers[0].cfg.decaySec = 0.48f;
        layers[0].cfg.sustainLevel = 0.70f; layers[0].cfg.releaseSec = 0.14f;
        layers[0].cfg.sawLevel = 0.65f; layers[0].cfg.pulseLevel = 0.35f;
        layers[0].cfg.subLevel = 0.85f; layers[0].cfg.drive = 0.23f;
        layers[1].cfg = SynthLayerConfig{}; layers[1].cfg.enabled = false;
        layers[2].cfg = SynthLayerConfig{}; layers[2].cfg.enabled = false;
    } else if (synthPresetId == 7) { // PWM Motion
        layers[0].cfg.waveType = 8; layers[0].cfg.sawLevel = 0.0f;
        layers[0].cfg.pulseLevel = 1.0f; layers[0].cfg.pwmDepth = 0.42f;
        layers[0].cfg.pwmRateHz = 1.1f; layers[0].cfg.filterEnvAmt = 0.35f;
        layers[1].cfg.waveType = 9; layers[1].cfg.osc2Level = 0.55f;
        layers[1].cfg.osc2Wave = 1; layers[1].cfg.pwmDepth = 0.36f;
        layers[1].cfg.pwmRateHz = 0.63f;
        layers[2].cfg.enabled = false;
    } else if (synthPresetId == 8) { // Jupiter Poly Synth
        layers[0].cfg.waveType = 9; layers[0].cfg.osc2Level = 0.75f;
        layers[0].cfg.osc2Detune = 11.0f; layers[0].cfg.pulseLevel = 0.30f;
        layers[1].cfg.waveType = 9; layers[1].cfg.osc2Level = 0.50f;
        layers[1].cfg.osc2Semitones = 7; layers[1].cfg.osc2Wave = 2;
        layers[2].cfg.waveType = 8; layers[2].cfg.subLevel = 0.35f;
        layers[2].cfg.pwmDepth = 0.15f;
    }
    // New bank: Keys, Bells, Cinematic Pads and Classic Brass.
    if (synthPresetId == 9) { // Analog Keys
        for (auto& l : layers) l.cfg = SynthLayerConfig{};
        auto& a=layers[0].cfg; a.enabled=true; a.waveType=8; a.volume=.82f;
        a.attackSec=.008f; a.decaySec=.85f; a.sustainLevel=.45f; a.releaseSec=.40f;
        a.cutoffHz=5800.f; a.pulseLevel=.55f; a.sawLevel=.50f; a.pwmDepth=.12f;
        auto& b=layers[1].cfg; b.enabled=true; b.waveType=3; b.octave=1; b.volume=.24f;
        b.attackSec=.003f; b.decaySec=.44f; b.sustainLevel=.18f; b.releaseSec=.25f;
    } else if (synthPresetId == 10) { // FM-like Glass Bells with partial wavetable
        for (auto& l : layers) l.cfg = SynthLayerConfig{};
        auto& a=layers[0].cfg; a.enabled=true; a.waveType=6; a.wavetableIdx=5;
        a.octave=1; a.attackSec=.003f; a.decaySec=1.5f; a.sustainLevel=.01f;
        a.releaseSec=1.9f; a.cutoffHz=12500.f; a.volume=.77f;
        auto& b=layers[1].cfg; b.enabled=true; b.waveType=3;
        b.octave=2; b.semitones=7; b.volume=.27f; b.attackSec=.002f;
        b.decaySec=1.0f; b.sustainLevel=.01f; b.releaseSec=1.6f;
    } else if (synthPresetId == 11) { // Soft Motion Pad
        for (auto& l : layers) l.cfg = SynthLayerConfig{};
        auto& a=layers[0].cfg; a.enabled=true; a.waveType=8; a.volume=.63f;
        a.attackSec=.9f; a.decaySec=3.2f; a.sustainLevel=.84f; a.releaseSec=3.0f;
        a.cutoffHz=3400.f; a.pulseLevel=.8f; a.sawLevel=.18f;
        a.pwmDepth=.34f; a.pwmRateHz=.19f;
        auto& b=layers[1].cfg; b=a; b.enabled=true; b.detuneCents=12.f;
        b.pan=.45f; b.volume=.48f; b.pwmRateHz=.26f;
        a.pan=-.45f;
        auto& c=layers[2].cfg; c.enabled=true; c.waveType=3; c.octave=-1;
        c.volume=.29f; c.attackSec=1.2f; c.releaseSec=3.2f;
    } else if (synthPresetId == 12) { // Vintage Brass Section
        for (auto& l : layers) l.cfg = SynthLayerConfig{};
        auto& a=layers[0].cfg; a.enabled=true; a.waveType=9;
        a.volume=.78f; a.cutoffHz=2200.f; a.filterEnvAmt=.82f;
        a.filtAttackSec=.035f; a.filtDecaySec=.48f; a.filtSustainLvl=.25f;
        a.attackSec=.045f; a.decaySec=.58f; a.sustainLevel=.70f; a.releaseSec=.30f;
        a.sawLevel=.88f; a.osc2Level=.62f; a.osc2Detune=9.f;
        auto& b=layers[1].cfg; b=a; b.enabled=true; b.pan=.28f;
        b.detuneCents=7.f; b.osc2Semitones=7; b.volume=.40f;
        a.pan=-.28f;
    }
    // Expanded factory bank 13–48, six families × six independently voiced variations.
    // Different IDs alter filter, oscillators, envelope and stereo layering (not just names).
    if (synthPresetId >= 13 && synthPresetId <= 48) {
        // Match the catalogue's actual families (12 Brass, 12 Leads, 12 Keys).
        const int family = synthPresetId <= 24 ? 2 : (synthPresetId <= 36 ? 4 : 0);
        const int variant = (synthPresetId - 13) % 12;
        const float v = static_cast<float>(variant % 6) + (variant >= 6 ? 0.65f : 0.0f);
        for (auto& layer : layers) layer.cfg = SynthLayerConfig{};
        auto& a = layers[0].cfg;
        a.enabled = true; a.volume = .72f; a.pan = 0.f;
        a.cutoffHz = std::min(15000.f, 1600.f + v * 1100.f);
        a.resonanceQ = .75f + v * .23f;
        a.filterKeyTrack = .25f + v * .08f;
        a.sawLevel = .78f; a.pulseLevel = .38f;
        a.subLevel = .18f; a.noiseLevel = 0.f;
        a.osc2Detune = 4.f + v * 3.f;
        a.pwmRateHz = .17f + v * .13f;
        a.attackSec=.01f; a.decaySec=.85f; a.sustainLevel=.6f; a.releaseSec=.4f;
        auto& b = layers[1].cfg;
        b = a; b.enabled = true; b.volume = .30f; b.pan = .30f;
        b.detuneCents = 4.f + 2.f*v; a.pan = -.22f;
        auto& c = layers[2].cfg;
        c = SynthLayerConfig{};
        switch (family) {
        case 0: // KEYS: tines, polykeys, classic DCO keyboards
            a.waveType = (variant % 3 == 0 ? 3 : 8);
            a.attackSec=.003f; a.decaySec=.32f + .22f*v;
            a.sustainLevel=.13f + .08f*v; a.releaseSec=.22f+.12f*v;
            a.cutoffHz=2600.f + v*1750.f; a.filterEnvAmt=.27f + .06f*v;
            b.waveType=(variant % 2 ? 2 : 3); b.octave = 1; b.volume=.22f;
            if (variant==4) { c.enabled=true; c.waveType=3; c.octave=2; c.volume=.12f; c.releaseSec=1.5f; }
            break;
        case 1: // PADS: evolving PWM, velvet analog, cinematic slow brass
            a.waveType=8; a.attackSec=.18f + .22f*v; a.decaySec=2.1f;
            a.sustainLevel=.72f+.03f*v; a.releaseSec=1.8f+.34f*v;
            a.pwmDepth=.13f+.055f*v; a.pwmRateHz=.12f+.075f*v;
            a.cutoffHz=1800.f+v*1050.f; a.pulseLevel=.80f;
            b=a; b.pan=.48f; a.pan=-.48f; b.detuneCents=7.f+3.f*v; b.volume=.52f;
            if (variant>2) { c.enabled=true; c.waveType=3; c.octave=-1; c.volume=.16f; c.attackSec=.9f; c.releaseSec=2.9f; }
            break;
        case 2: // BRASS: resonant filter envelope, dual DCO, ensemble voicing
            a.waveType=9; a.osc2Level=.45f+.08f*v; a.osc2Detune=6.f+v*2.f;
            a.attackSec=.016f+.007f*v; a.decaySec=.47f;
            a.sustainLevel=.58f; a.releaseSec=.22f+.08f*v;
            a.cutoffHz=900.f + 400.f*v; a.filterEnvAmt=.56f+.065f*v;
            a.filtAttackSec=.015f; a.filtDecaySec=.27f+.09f*v; a.filtSustainLvl=.17f;
            b=a; b.pan=.26f; a.pan=-.26f; b.detuneCents=5.f+v*2.f; b.volume=.44f;
            if (variant%2==0) b.osc2Semitones=7;
            break;
        case 3: // BASS: octave subs, PWM bass, gritty Jupiter bass
            a.waveType=variant%2 ? 9 : 8;
            a.cutoffHz=380.f+v*330.f; a.subLevel=.65f+.045f*v;
            a.osc2Level=variant%2 ? .42f : 0.f;
            a.resonanceQ=.9f+.28f*v; a.drive=.09f+.065f*v;
            a.filterEnvAmt=.57f; a.filtDecaySec=.21f+.04f*v;
            a.filtSustainLvl=.09f; a.attackSec=.002f;
            a.decaySec=.35f+.06f*v; a.sustainLevel=.54f; a.releaseSec=.12f+.035f*v;
            b.enabled=false; c.enabled=false;
            if (variant>3) { b=a; b.enabled=true; b.octave=-1; b.volume=.21f; b.subLevel=.24f; }
            break;
        case 4: // LEADS: pulse/saw dual VCO, interval harmonics, portamento-like legato tone
            a.waveType=variant%2 ? 9 : 4;
            a.unisonVoices=(variant%3==0 ? 3 : 2);
            a.unisonDetune=5.f+4.f*v; a.unisonSpread=.20f+.08f*v;
            a.osc2Level=variant%2 ? .62f : 0.f;
            a.cutoffHz=2800.f+v*1900.f; a.filterEnvAmt=.19f;
            a.attackSec=.004f; a.decaySec=.3f; a.sustainLevel=.85f;
            a.releaseSec=.20f+.08f*v; a.drive=.10f+.06f*v;
            b.enabled=(variant>2); b.volume=.23f;
            b.octave=(variant==5 ? 1 : 0);
            break;
        default: // SAWS: wide supersaws, PWM saw poly, digital hybrids
            a.waveType=variant%2 ? 9 : 4; a.unisonVoices=3+(variant%3);
            a.unisonDetune=10.f+v*4.f; a.unisonSpread=.48f+.08f*v;
            a.osc2Level=variant%2 ? .7f : 0.f;
            a.cutoffHz=4600.f+v*1600.f;
            a.attackSec=.006f; a.decaySec=1.1f;
            a.sustainLevel=.8f; a.releaseSec=.5f+.15f*v;
            b=a; b.enabled=true; b.pan=.42f; a.pan=-.42f;
            b.detuneCents=8.f+v*2.f; b.volume=.33f;
            if (variant==5) { c.enabled=true; c.waveType=8; c.octave=-1; c.volume=.2f; c.releaseSec=1.6f; }
            break;
        }
        // The second half of each 12-patch family has a distinct oscillator colour.
        if (variant >= 6) {
            a.resonanceQ += .30f;
            a.osc2Level = .20f + .03f*(variant-6);
            a.drive = .05f + .025f*(variant-6);
            if (b.enabled) b.detuneCents += 3.f;
        }
        // Sound-specific oscillator designs matching named presets.
        if (synthPresetId == 16 || synthPresetId == 27 || synthPresetId == 37 || synthPresetId == 47)
            a.waveType = 10; // FM brass/lead/tines
        if (synthPresetId == 26 || synthPresetId == 29) a.waveType = 7; // sync lead
        if (synthPresetId == 34) a.waveType = 14; // wavefolder lead
        if (synthPresetId == 35 || synthPresetId == 41 || synthPresetId == 44) a.waveType = 13; // formant/organ
        if (synthPresetId == 36) a.waveType = 12; // ring modulation
    }

    // Expansion 49..120: twelve identifiable patches in each sound family.
    // Unlike the old clamp(48), every patch has its own oscillator/filter/envelope configuration.
    if (synthPresetId >= 49) {
        const int group = (synthPresetId - 49) / 12; // POLY, PAD, STRINGS, BASS, BELLS, HYBRID
        const int variant = (synthPresetId - 49) % 12;
        const float t = static_cast<float>(variant) / 11.0f;
        for (auto& layer : layers) layer.cfg = SynthLayerConfig{};
        auto& a = layers[0].cfg;
        auto& b = layers[1].cfg;
        auto& c = layers[2].cfg;
        a.enabled = true; a.volume = .78f; a.resonanceQ = .78f + .65f*t;
        a.filterKeyTrack = .35f; a.velToCutoff = .40f;
        a.osc2Detune = 3.f + variant * 1.9f;
        a.cutoffHz = 2600.f + variant * 390.f;
        a.attackSec = .012f; a.decaySec = .55f; a.sustainLevel = .65f; a.releaseSec = .48f;
        switch (group) {
        case 0: // Poly keys: analog poly, PPG, organ key, glass, 3-layer sound.
            a.waveType = (variant==4 ? 6 : (variant==8 ? 10 : (variant==9 ? 13 : 8)));
            a.attackSec = .002f; a.decaySec = .20f + t*.60f;
            a.sustainLevel = .18f + .40f*t; a.releaseSec = .22f+t*.54f;
            a.osc2Level = .3f; a.filterEnvAmt = .25f;
            b=a; b.enabled=true; b.volume=.26f; b.pan=.35f; a.pan=-.35f;
            b.waveType=(variant%3==0 ? 3 : (variant%3==1 ? 9 : 2));
            b.octave=(variant==2 || variant==11 ? 1 : 0);
            if (variant==11 || variant==4) {c=a; c.enabled=true; c.waveType=3; c.octave=1; c.volume=.16f;}
            break;
        case 1: // Pads / choirs / formant drones / sweeping filters.
            a.waveType = (variant==1 || variant==8 ? 13 : (variant==2 ? 10 : 8));
            a.attackSec=.18f+t*.95f; a.releaseSec=2.1f+t*2.0f;
            a.decaySec=1.8f; a.sustainLevel=.82f;
            a.cutoffHz=1450.f+t*4200.f; a.pwmDepth=.15f+t*.16f;
            a.pwmRateHz=.10f+t*.37f; a.filterEnvAmt=.19f;
            a.filterType=(variant==5 ? 2 : variant==6 ? 1 : 0);
            b=a; b.enabled=true; b.pan=.49f; a.pan=-.49f;
            b.detuneCents=6.f+variant; b.volume=.47f;
            if (variant%3==2) {c=a;c.enabled=true;c.waveType=3;c.octave=-1;c.volume=.17f;c.attackSec=.8f;}
            break;
        case 2: // Strings / Solina / synthetic ensembles.
            a.waveType=(variant==3 ? 13 : 8); a.pulseLevel=.76f;
            a.pwmDepth=.19f+t*.18f; a.pwmRateHz=.19f+t*.31f;
            a.attackSec=.025f+t*.28f; a.releaseSec=1.3f+t*1.0f;
            a.sustainLevel=.78f; a.cutoffHz=1800.f+t*5500.f;
            b=a; b.enabled=true; b.pan=.45f; a.pan=-.45f;
            b.detuneCents=8.f+t*11.f; b.volume=.50f;
            if (variant==6 || variant==11) {c=a;c.enabled=true;c.octave=-1;c.volume=.20f;}
            break;
        case 3: // Deep bass / Reese / FM / pulse / sub.
            a.waveType=(variant==2 || variant==5 ? 10 : (variant==1 ? 11 : 9));
            a.subLevel=.55f+t*.35f; a.pulseLevel=.4f;
            a.osc2Level=.40f; a.cutoffHz=260.f+t*1300.f;
            a.attackSec=.002f; a.decaySec=.22f+t*.35f;
            a.sustainLevel=.55f; a.releaseSec=.12f+t*.21f;
            a.filterEnvAmt=.56f+t*.31f; a.drive=.08f+t*.17f;
            if (variant==1 || variant==6) {b=a;b.enabled=true;b.waveType=11;b.volume=.27f;b.detuneCents=13.f;}
            break;
        case 4: // Bells / mallets / plucked FM / wavefolder.
            a.waveType=(variant%4==0 ? 14 : (variant%4==1 ? 10 : (variant%4==2 ? 6 : 12)));
            a.attackSec=.002f; a.decaySec=.36f+t*2.15f;
            a.sustainLevel=.012f; a.releaseSec=.35f+t*1.55f;
            a.cutoffHz=4500.f+t*9100.f; a.filterEnvAmt=.16f;
            b=a; b.enabled=(variant%3!=0); b.volume=.28f; b.pan=.31f;
            b.octave=1; b.waveType=3; a.pan=-.12f;
            if (variant==8 || variant==11) {c=a;c.enabled=true;c.volume=.11f;c.octave=2;c.waveType=3;}
            break;
        default: // Hybrid stage stacks: saw / brass / leads / cinematic.
            a.waveType=(variant%4==0 ? 4 : variant%4==1 ? 9 : variant%4==2 ? 7 : 14);
            a.cutoffHz=2200.f+t*8500.f;
            a.attackSec=(variant%3==1 ? .30f : .006f);
            a.releaseSec=(variant%3==1 ? 2.0f : .45f+t*.7f);
            a.filterEnvAmt=.38f+t*.29f;
            a.unisonVoices=2+(variant%3); a.unisonDetune=7.f+t*13.f;
            a.unisonSpread=.35f+t*.30f; a.osc2Level=.57f;
            b=a; b.enabled=true; b.volume=.38f; b.pan=.39f;
            b.detuneCents=5.f+t*9.f; a.pan=-.39f;
            if (variant%3==2) {c=a;c.enabled=true;c.volume=.18f;c.octave=-1;c.waveType=8;}
            break;
        }
    }
    for (auto& l : layers) l.reset();
}

void NordSynthModule::noteOn(int rawKey, int playedKey, int velocity) {
    if (!synthEnabled) return;
    if (synthVoiceMode != 0) {
        monoHeldNotes.erase(std::remove_if(monoHeldNotes.begin(),monoHeldNotes.end(),
            [rawKey](const HeldNote& n){return n.rawKey==rawKey;}),monoHeldNotes.end());
        monoHeldNotes.push_back({rawKey,playedKey,velocity});
    }
    for (auto& l : layers) {
        l.voiceMode=synthVoiceMode;
        l.glideSeconds=synthPortamento;
        l.glideMode=synthGlideMode;
        l.slideSemis=synthSlide;
        l.noteOn(rawKey, playedKey, velocity);
    }
}

void NordSynthModule::noteOff(int rawKey, bool pedalDown) {
    if (synthVoiceMode != 0) {
        const bool current=!monoHeldNotes.empty() && monoHeldNotes.back().rawKey==rawKey;
        monoHeldNotes.erase(std::remove_if(monoHeldNotes.begin(),monoHeldNotes.end(),
            [rawKey](const HeldNote& n){return n.rawKey==rawKey;}),monoHeldNotes.end());
        if (!current) return; // only the highest-priority held key owns the voice
        if (!monoHeldNotes.empty()) {
            const auto n=monoHeldNotes.back();
            for (auto& l:layers) l.noteOn(n.rawKey,n.playedKey,n.velocity);
            return;
        }
    }
    for (auto& l : layers) l.noteOff(rawKey, pedalDown);
}

void NordSynthModule::setSustain(bool down) {
    for (auto& l : layers) l.setSustain(down);
}

void NordSynthModule::allNotesOff() {
    monoHeldNotes.clear();
    for (auto& l : layers) l.reset();
}

void NordSynthModule::render(float* outInterleavedStereo, unsigned int numFrames,
                             const std::array<float*, 3>* layerStems) {
    if (!synthEnabled || synthMasterVol <= 0.001f) return;
    if (bufL_.size() < numFrames) {
        bufL_.resize(numFrames);
        bufR_.resize(numFrames);
    }
    updateLFOsAndEnvs(numFrames, sr_);
    for (int layer = 0; layer < 3; ++layer) {
        std::fill(bufL_.begin(), bufL_.begin() + numFrames, 0.0f);
        std::fill(bufR_.begin(), bufR_.begin() + numFrames, 0.0f);
        layers[layer].renderLayer(bufL_.data(), bufR_.data(), numFrames, sr_,
                                 synthMacroCutoff, wavetableBank, lfos.data(), envelopes.data());
        float* stem = layerStems ? (*layerStems)[layer] : outInterleavedStereo;
        for (unsigned int n = 0; n < numFrames; ++n) {
            stem[n * 2]     += bufL_[n] * synthMasterVol;
            stem[n * 2 + 1] += bufR_[n] * synthMasterVol;
        }
    }
}

} // namespace NordDSP