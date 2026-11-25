#include "StageOrganModule.h"
#include "../dsp/StageDspEffects.h"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace SonusDSP {

namespace {
constexpr double HARMONICS[9] = {0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};
constexpr float LEAKAGE = 0.008f;
constexpr float CROSSOVER_HZ = 760.0f;
// Distinct spectral voicings. Non-B3 models use tone generation instead of
// merely changing the model label, with bounded output and alias-safe partials.
// sin(2*pi*x) for x in [0,1): odd 9th-order polynomial after folding to [-pi/2, pi/2]
// (max error ~4e-6, below -100 dB). Replaces std::sin(double) in the tonewheel loop,
// which ran 9 times per voice per sample.
inline float fastSin(float x) {
    float y = x - 0.5f;                       // sin(2*pi*x) = -sin(2*pi*y)
    if (y > 0.25f) y = 0.5f - y; else if (y < -0.25f) y = -0.5f - y;
    const float t = y * 6.28318531f, t2 = t * t;
    return -(t * (1.f + t2 * (-0.166666667f + t2 * (0.00833333333f + t2 * (-0.000198412698f + t2 * 2.75573192e-6f)))));
}
inline float frac01(float x) { return x - float(int(x)); }    // x >= 0
// Distinct spectral voicings. Non-B3 models use tone generation instead of
// merely changing the model label, with bounded output and alias-safe partials.
inline float modelOscillator(int model, float phase) {        // phase in [0,1)
    const float sinus = fastSin(phase);
    if(model==0) return sinus; // Common B3 path: exactly one oscillator sine.
    // High harmonics are only evaluated in models that use them.
    const float second = fastSin(frac01(phase * 2.f));
    switch(model) {
      case 1: return 0.64f * sinus + 0.36f * second;                // Vox transistor combo
      case 2: return 0.58f * std::tanh(2.7f * sinus) + 0.42f * (1.f - 4.f * std::abs(phase-.5f)); // Farfisa
      case 3: return 0.82f * sinus + 0.12f * second + 0.06f * fastSin(frac01(phase * 3.f)); // pipe flute
      case 4: return 0.92f * sinus + 0.08f * second;                // mellow B3
      case 5: return 0.78f * sinus + 0.22f * second;                // bright B3
      case 6: return 0.55f * sinus + 0.45f * std::tanh(3.5f * sinus); // combo transistor
      case 7: return 0.76f * sinus + 0.15f * second + 0.09f * fastSin(frac01(phase * 4.f)); // theatre pipe
      default: return sinus;                                      // B3
    }
}
inline float spectralGain(int model, int d) {
    float g = 1.0f;
    if(model==4) g = 1.0f - 0.065f * d;
    else if(model==5) g = 0.78f + 0.055f * d;
    else if(model==3) g = 1.0f - 0.08f * d;
    else if(model==7) g = 1.0f - 0.045f * d;
    return std::max(0.3f, g);
}
inline float overtoneFactor(int model) { return model==3?3.f:model==7?4.f:(model==0?1.f:2.f); }
} // namespace

void StageOrganModule::init(float sr) {
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    for (auto& v : organVoices) {
        v.active = false;
        v.env = 0.0f;
        v.stage = 0;
    }
    horn_.phase = 0.0;
    horn_.currentRPS = LESLIE_SLOW_RPS;
    drum_.phase = 0.25;
    drum_.currentRPS = LESLIE_SLOW_RPS * 0.85;
    horn_.accelTau = 0.50; horn_.decelTau = 0.90;
    drum_.accelTau = 1.70; drum_.decelTau = 2.40;
    hornDelay_.fill(0.0f);drumDelay_.fill(0.0f);rotorWritePos_ = 0;
    drumLP_state_ = 0.0f;
    ownChorus_.init(sr_);
    vibWritePos_ = 0;
    vibLfoPhase_ = 0.0;
    wheelPhase_ = 0.0;
    vibDelayLine_.fill(0.0f);
    clickEnvelope_ = 0.0f;
    smoothPrimed_ = false;
    snapSmoothing();
}

// Jump every smoothed control to its target: only while the organ is silent.
void StageOrganModule::snapSmoothing() {
    for (int d = 0; d < 9; ++d) drawbarSmooth_[d] = std::clamp(drawbars[d], 0.f, 1.f);
    volumeSmooth_ = organEnabled ? std::clamp(organVolume, 0.f, 1.5f) : 0.f;
    driveSmooth_ = std::clamp(organDrive, 0.f, 1.f);
    currentModel_ = previousModel_ = std::clamp(organModel, 0, 7);
    modelMix_ = 1.f;
    smoothPrimed_ = true;
}

bool StageOrganModule::loadOrganSample(const std::string& path) {
    // Bounded RIFF/WAVE reader: walks chunks by their declared sizes, accepts
    // PCM16 mono/stereo only and caps the decoded size. Results are built in
    // locals and only published on success.
    constexpr uint32_t MAX_DATA_BYTES = 256u * 1024u * 1024u;
    auto le16 = [](const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); };
    auto le32 = [](const unsigned char* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    };
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char riff[12];
    if (!f.read(reinterpret_cast<char*>(riff), 12)) return false;
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return false;

    uint32_t format = 0, numChannels = 0, bitsPerSample = 0;
    bool haveFmt = false;
    std::vector<char> raw;
    unsigned char ck[8];
    while (f.read(reinterpret_cast<char*>(ck), 8)) {
        const uint32_t size = le32(ck + 4);
        if (std::memcmp(ck, "fmt ", 4) == 0) {
            if (size < 16) return false;
            unsigned char fmt[16];
            if (!f.read(reinterpret_cast<char*>(fmt), 16)) return false;
            format = le16(fmt);
            numChannels = le16(fmt + 2);
            bitsPerSample = le16(fmt + 14);
            haveFmt = true;
            f.seekg(std::streamoff(size - 16 + (size & 1u)), std::ios::cur);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            if (!haveFmt || size == 0 || size > MAX_DATA_BYTES) return false;
            raw.resize(size);
            if (!f.read(raw.data(), std::streamsize(size))) return false;
            break;
        } else {
            f.seekg(std::streamoff(size) + std::streamoff(size & 1u), std::ios::cur);
        }
        if (!f) return false;
    }
    // WAVE_FORMAT_PCM (1) or WAVE_FORMAT_EXTENSIBLE (0xFFFE) carrying PCM16.
    if (raw.empty() || (format != 1 && format != 0xFFFE) || bitsPerSample != 16 ||
        (numChannels != 1 && numChannels != 2)) return false;

    const size_t frames = raw.size() / (2u * numChannels);
    if (frames == 0) return false;
    std::vector<float> left(frames), right(frames);
    const auto* bytes = reinterpret_cast<const unsigned char*>(raw.data());
    auto sampleAt = [&](size_t index) {
        return float(int16_t(le16(bytes + index * 2))) / 32768.0f;
    };
    for (size_t i = 0; i < frames; ++i) {
        left[i] = sampleAt(i * numChannels);
        right[i] = numChannels == 2 ? sampleAt(i * 2 + 1) : left[i];
    }
    organSampleL = std::move(left);
    organSampleR = std::move(right);
    organSamplePath = path;
    return true;
}

void StageOrganModule::noteOn(int rawKey, int playedKey, int velocity) {
    if (!organEnabled) return;

    // En un Hammond B3 real, la percusión es "Single-Triggered" (solo dispara si no había otra tecla retenida)
    bool anyHeldBefore = false;
    OrganVoice* target = nullptr;
    for (auto& v : organVoices) {
        if (v.active && v.keyHeld && v.rawKey != rawKey) anyHeldBefore = true;
        // Retrigger: fade the old oscillator instead of replacing it mid-cycle.
        if (v.active && v.rawKey == rawKey) {
            // Reuse the active tonewheel without touching phase1.
            if(!target)target=&v;
            else {v.keyHeld=false;v.sustained=false;v.stage=4;}
            continue;
        }
    }
    const int allowed=std::clamp(voiceLimit,1,ORGAN_MAX_VOICES);
    if (!target) {
        for(int i=0;i<allowed;++i){
            auto& v=organVoices[size_t(i)];
            if(!v.active){target=&v;break;}
        }
    }
    bool stolen=false;
    if(!target){
        // Prefer stealing a releasing or quiet voice, never exceed allowed slots.
        target=&organVoices[0];
        for(int i=1;i<allowed;++i){
            auto& v=organVoices[size_t(i)];
            if((!v.keyHeld && target->keyHeld) || v.env<target->env)target=&v;
        }
        // v32: the stolen tone finishes with the 7 ms anti-click release in a spare
        // slot above the budget (rendered until silent) instead of being retuned
        // at full level. The new key then starts from its own attack.
        if(target->active && target->env>0.f){
            for(int i=allowed;i<ORGAN_MAX_VOICES;++i){
                auto& spare=organVoices[size_t(i)];
                if(spare.active)continue;
                spare=*target; spare.keyHeld=false; spare.sustained=false; spare.stage=4;
                stolen=true; break;
            }
        }
    }

    target->active = true;
    target->rawKey = rawKey;
    target->keyHeld = true;
    target->sustained = false;
    target->stage = 3;
    target->env = stolen ? 0.f : std::min(target->env, 1.f); // retain envelope on same-key retrigger
    if (stolen) target->phase1 = 0.0;
    target->velGain = 0.85f + (velocity / 127.0f) * 0.15f; // El órgano es casi constante en velocidad
    target->freq = 440.0 * std::pow(2.0, (playedKey - 69.0) / 12.0);

    if (percussionEnabled && !anyHeldBefore) {
        target->percEnv = 1.0;
        target->percActive = true;
        target->percRamp = 0.f;
    } else {
        target->percEnv = 0.0;
        target->percActive = false;
    }

    // Subtle Hammond key-click, not an audible transient spike.
    clickEnvelope_ = 0.07f;
    clickSample_ = ((rawKey & 1) ? 1.0f : -1.0f) * 0.015f;
}

void StageOrganModule::noteOff(int rawKey, bool pedalDown) {
    for (auto& v : organVoices) {
        if (v.active && v.rawKey == rawKey && v.keyHeld) {
            v.keyHeld = false;
            if (pedalDown) v.sustained = true;
            else v.stage = 4; // Caída anti-click de 4 ms
        }
    }
}

void StageOrganModule::setSustain(bool down) {
    if (!down) {
        for (auto& v : organVoices) {
            if (v.active && !v.keyHeld && v.sustained) {
                v.sustained = false;
                v.stage = 4;
            }
        }
    }
}

void StageOrganModule::allNotesOff() {
    // Panic / INIT: every wheel goes through the 7 ms anti-click release.
    for (auto& v : organVoices) {
        v.keyHeld = false;
        v.sustained = false;
        if (v.active) v.stage = 4;
    }
}

void StageOrganModule::render(float* outInterleavedStereo, unsigned int numFrames) {
    if (!smoothPrimed_) snapSmoothing();
    if (!needsRender()) {
        // Switched off and fully faded: release the voices so they never resume.
        if (!organEnabled) for (auto& v : organVoices) { v.active = false; v.keyHeld = false; v.sustained = false; }
        snapSmoothing();
        return;
    }
    // No active tonewheels: avoid oscillators, LFO and rotary entirely.
    bool sounding = clickEnvelope_ > 0.001f;
    if(!sounding)for(const auto& v:organVoices)if(v.active){sounding=true;break;}
    if(!sounding){ snapSmoothing(); return; }

    // Control targets are read ONCE per block (they are written by the UI / MIDI
    // threads) and every one is ramped per sample: no zipper or clicks while
    // drawbars, volume, drive or the model change with keys held.
    std::array<float, 9> drawbarTarget{};
    for (int d = 0; d < 9; ++d) drawbarTarget[d] = std::clamp(drawbars[d], 0.f, 1.f);
    const float volumeTarget = organEnabled ? std::clamp(organVolume, 0.f, 1.5f) : 0.f;
    const float driveTarget = std::clamp(organDrive, 0.f, 1.f);
    const int requestedModel = std::clamp(organModel, 0, 7);
    if (requestedModel != currentModel_) {
        // A second change during a crossfade restarts it from the model now heard most.
        previousModel_ = modelMix_ >= 0.5f ? currentModel_ : previousModel_;
        currentModel_ = requestedModel;
        modelMix_ = 0.f;
    }
    const float smoothK = 1.0f - std::exp(-1.0f / (0.010f * sr_));   // 10 ms controls
    const float modelStep = 1.0f / (0.025f * sr_);                    // 25 ms model crossfade
    const int modelA = currentModel_, modelB = previousModel_;

    const double fastRate=std::clamp(double(rotaryFastRate),0.6,1.5);
    const double hornTarget = organRotaryFast ? LESLIE_FAST_RPS*fastRate : LESLIE_SLOW_RPS;
    const double drumTarget = organRotaryFast ? LESLIE_FAST_DRUM_RPS*fastRate : (LESLIE_SLOW_RPS * 0.85);
    const double wc = 2.0 * PI_F * CROSSOVER_HZ / sr_;
    const float lpCoef = static_cast<float>(wc / (wc + 1.0));
    const bool pipeModel = (modelA == 3 || modelA == 7);
    const float releaseStep = 1.0f / ((pipeModel ? 0.16f : 0.007f) * sr_);
    const float attackStep = 1.0f / ((pipeModel ? 0.065f : 0.005f) * sr_);
    const double percDecayCoef = std::exp(-4.8 / (std::max(0.08f, percussionDecaySec) * sr_));

    const double vibRateHz = std::clamp(vibratoRate, 2.0f, 12.0f);
    const double vibInc = vibRateHz / sr_;
    float vibDepthSamples = 0.0f;
    bool useChorus = (vibratoMode >= 4);
    switch (vibratoMode) {
        case 1: case 4: vibDepthSamples = 1.8f; break;
        case 2: case 5: vibDepthSamples = 3.4f; break;
        case 3: case 6: vibDepthSamples = 5.5f; break;
        default: vibDepthSamples = 0.0f; break;
    }

    vibDepthSamples *= std::clamp(vibratoDepth, 0.0f, 2.0f);
    const double pitchMul = std::pow(2.0, std::clamp(pitchBendSemis, -2.0f, 2.0f) / 12.0f);
    const float wheelAmount = std::clamp(modWheel, 0.0f, 1.0f);
    const float rotaryWet = std::clamp(rotaryMix, 0.0f, 1.0f);
    const bool useRotary = rotaryWet > 0.0001f;
    // Compute motor poles once per audio callback, never exp() per sample.
    const double ramp=std::clamp(double(rotaryRamp),0.4,2.5);
    const float hornAccel = useRotary ? static_cast<float>(std::exp(-1.0 / (horn_.accelTau * ramp * sr_))) : 0.f;
    const float hornDecel = useRotary ? static_cast<float>(std::exp(-1.0 / (horn_.decelTau * ramp * sr_))) : 0.f;
    const float drumAccel = useRotary ? static_cast<float>(std::exp(-1.0 / (drum_.accelTau * ramp * sr_))) : 0.f;
    const float drumDecel = useRotary ? static_cast<float>(std::exp(-1.0 / (drum_.decelTau * ramp * sr_))) : 0.f;
    auto readRotorDelay = [](const auto& buf,int pos,float delay)->float {
      float absolute=static_cast<float>(pos)-delay;
      if(absolute<0.f)absolute+=static_cast<float>(ROTOR_DELAY_SIZE);
      const int i=static_cast<int>(absolute)&(ROTOR_DELAY_SIZE-1);
      const float fraction=absolute-static_cast<float>(static_cast<int>(absolute));
      return buf[i]*(1.f-fraction)+buf[(i+1)&(ROTOR_DELAY_SIZE-1)]*fraction;
    };
    // Alias guard per voice and per block (the wheel vibrato adds at most +22 cents).
    // Bit d set = footage d stays below Nyquist for the current / previous model.
    const double maxPitch = pitchMul * 1.0128;
    const float nyquistGuard = sr_ * 0.46f;
    std::array<uint16_t, ORGAN_MAX_VOICES> maskA{}, maskB{};
    for (size_t i = 0; i < organVoices.size(); ++i) {
        const auto& v = organVoices[i];
        if (!v.active) continue;
        for (int d = 0; d < 9; ++d) {
            const float hz = static_cast<float>(v.freq * maxPitch * HARMONICS[d]);
            if (hz * overtoneFactor(modelA) < nyquistGuard) maskA[i] |= uint16_t(1u << d);
            if (hz * overtoneFactor(modelB) < nyquistGuard) maskB[i] |= uint16_t(1u << d);
        }
    }
    double wheelPhase = wheelPhase_;
    for (unsigned int n = 0; n < numFrames; ++n) {
        double wheelPitch=1.0;
        if(wheelAmount>0.0001f){
          wheelPhase += 5.2 / sr_;
          if (wheelPhase >= 1.0) wheelPhase -= 1.0;
          wheelPitch=std::pow(2.0,wheelAmount*22.0*std::sin(2.0*PI_F*wheelPhase)/1200.0);
        }
        if(useRotary){
          auto updateRotor = [&](LeslieRotor& r,double target,float accel,float decel){
            const float coef=target>r.currentRPS?accel:decel;
            r.currentRPS=target+coef*(r.currentRPS-target);
            r.phase+=r.currentRPS/sr_;if(r.phase>=1.0)r.phase-=1.0;
          };
          updateRotor(horn_,hornTarget,hornAccel,hornDecel);
          updateRotor(drum_,drumTarget,drumAccel,drumDecel);
        }

        // Per-sample control ramps, shared by every voice.
        for (int d = 0; d < 9; ++d) drawbarSmooth_[d] += (drawbarTarget[d] - drawbarSmooth_[d]) * smoothK;
        volumeSmooth_ += (volumeTarget - volumeSmooth_) * smoothK;
        if (volumeTarget <= 0.f && volumeSmooth_ < 1e-4f) volumeSmooth_ = 0.f;   // below -80 dB: done
        driveSmooth_ += (driveTarget - driveSmooth_) * smoothK;
        if (modelMix_ < 1.f) modelMix_ = std::min(1.f, modelMix_ + modelStep);
        const bool crossfading = modelMix_ < 1.f;
        float level[9], gainA[9], gainB[9];
        for (int d = 0; d < 9; ++d) {
            level[d] = drawbarSmooth_[d];
            if (d > 0) level[d] += drawbarSmooth_[d - 1] * LEAKAGE;
            if (d < 8) level[d] += drawbarSmooth_[d + 1] * LEAKAGE;
            gainA[d] = level[d] * spectralGain(modelA, d) * (crossfading ? modelMix_ : 1.f);
            gainB[d] = crossfading ? level[d] * spectralGain(modelB, d) * (1.f - modelMix_) : 0.f;
        }

        float orgSum = 0.0f;
        float percSum = 0.0f;

        for (size_t vi = 0; vi < organVoices.size(); ++vi) {
            auto& v = organVoices[vi];
            if (!v.active) continue;

            if (v.stage == 3 && v.env < 1.0f) {
                v.env = std::min(1.0f, v.env + attackStep);
            }
            if (v.stage == 4) {
                v.env -= releaseStep;
                if (v.env <= 0.0f) {
                    v.env = 0.0f;
                    v.active = false;
                    continue;
                }
            }

            const double p = v.phase1;
            float noteSample = 0.0f;
            const unsigned bitsA = maskA[vi], bitsB = crossfading ? maskB[vi] : 0u;
            for (int d = 0; d < 9; ++d) {
                if (level[d] <= 0.005f) continue;
                const unsigned bit = 1u << d;
                if (!((bitsA | bitsB) & bit)) continue;
                const double cycles = p * HARMONICS[d];
                const float phase = static_cast<float>(cycles - static_cast<double>(static_cast<int>(cycles)));
                if (bitsA & bit) noteSample += modelOscillator(modelA, phase) * gainA[d];
                if (bitsB & bit) noteSample += modelOscillator(modelB, phase) * gainB[d];
            }

            // CORRECCIÓN CLAVE: Envolver en 2.0 para que 0.5 (16') y 1.5 (5 1/3') completen ciclos enteros
            v.phase1 += v.freq * pitchMul * wheelPitch / sr_;
            if (v.phase1 >= 2.0) v.phase1 -= 2.0;

            if (percussionEnabled && v.percActive && v.percEnv > 0.001) {
                const float percHarm = (percussionHarmonic == 2) ? 2.0f : 3.0f;
                const float percS = fastSin(frac01(static_cast<float>(p) * percHarm));
                v.percRamp = std::min(1.f, v.percRamp + 1.f/(0.003f*sr_));
                percSum += percS * static_cast<float>(v.percEnv) * percussionLevel * v.env * v.percRamp;
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

        if (driveSmooth_ > 0.0005f) {
            // Blend in over the first 5 % of the knob: the old ON threshold at 0.01
            // jumped the level by +2.4 dB. Uses the smoothed amount.
            const float pre = 1.0f + driveSmooth_ * 4.5f;
            const float driven = std::tanh(orgSum * pre) / std::tanh(pre) * (1.0f + driveSmooth_ * 0.25f);
            const float amount = std::min(1.f, driveSmooth_ / 0.05f);
            orgSum += (driven - orgSum) * amount;
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

        float finalL=outL*volumeSmooth_, finalR=outR*volumeSmooth_;
        if(useRotary){
          // Frequency-dependent cabinet crossover, then physical rotating delay
          // paths. Fractional delay => Doppler; opposition => stereo mic motion.
          drumLP_state_ += lpCoef*(outL-drumLP_state_);
          const float bass=drumLP_state_,treble=outL-bass;
          const double hornTheta=2.0*PI_F*horn_.phase;
          const double drumTheta=2.0*PI_F*drum_.phase;
          const float hs=static_cast<float>(std::sin(hornTheta));
          const float hc=static_cast<float>(std::cos(hornTheta));
          const float ds=static_cast<float>(std::sin(drumTheta));
          const float dc=static_cast<float>(std::cos(drumTheta));
          hornDelay_[rotorWritePos_]=treble;
          drumDelay_[rotorWritePos_]=bass;
          const float hL=readRotorDelay(hornDelay_,rotorWritePos_,32.f+11.f*hs);
          const float hR=readRotorDelay(hornDelay_,rotorWritePos_,32.f-11.f*hs);
          const float dL=readRotorDelay(drumDelay_,rotorWritePos_,49.f+15.f*ds);
          const float dR=readRotorDelay(drumDelay_,rotorWritePos_,49.f-15.f*ds);
          rotorWritePos_=(rotorWritePos_+1)&(ROTOR_DELAY_SIZE-1);
          // Directional microphone response and cabinet crossfeed; normalized
          // so speed changes do not cause pronounced master gain pumping.
          const float wetL=(hL*(0.70f+0.22f*hc)+dL*(0.74f+0.15f*dc))*1.09f;
          const float wetR=(hR*(0.70f-0.22f*hc)+dR*(0.74f-0.15f*dc))*1.09f;
          // Width is mid/side on the physical cabinet return only; 1.0
          // preserves the original sound, 0.0 is phase-safe mono.
          const float center=(wetL+wetR)*0.5f;
          const float side=(wetL-wetR)*0.5f*std::clamp(rotaryStereoWidth,0.0f,1.5f);
          finalL=(outL*(1.f-rotaryWet)+(center+side)*rotaryWet)*volumeSmooth_;
          finalR=(outR*(1.f-rotaryWet)+(center-side)*rotaryWet)*volumeSmooth_;
        }

        // Independent ensemble chorus on the organ output, after scanner and Leslie.
        // Even when bypassed the delay line keeps running to prevent stale-buffer clicks.
        ownChorus_.processStereo(finalL, finalR,
            false, 1.0f, 0.0f, false, 1.0f, 0.0f, 0.0f, 0.0f,
            chorusEnabled, chorusRate, chorusDepth, chorusMix,
            chorusVoices, chorusWidth, chorusFeedback, chorusTone);
        outInterleavedStereo[n * 2]     += finalL;
        outInterleavedStereo[n * 2 + 1] += finalR;
    }
    wheelPhase_ = wheelPhase;
    // Switched OFF: once the output ramp is silent, free every voice.
    if (!organEnabled && volumeSmooth_ < 1e-6f) {
        volumeSmooth_ = 0.f;
        for (auto& v : organVoices) { v.active = false; v.keyHeld = false; v.sustained = false; }
    }
}

} // namespace SonusDSP