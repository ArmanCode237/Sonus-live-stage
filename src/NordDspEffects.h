#pragma once
#include "NordTypes.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>

namespace NordDSP {

constexpr float PI_F = 3.14159265358979323846f;

class StateVariableFilter {
public:
    void reset() { ic1eqL_ = ic2eqL_ = ic1eqR_ = ic2eqR_ = 0.0f; }

    void setParams(float sampleRate, float cutoffHz, float resonanceQ) {
        cutoffHz = std::clamp(cutoffHz, 20.0f, std::min(20000.0f, sampleRate * 0.48f));
        resonanceQ = std::clamp(resonanceQ, 0.5f, 15.0f);
        g_ = std::tan(PI_F * cutoffHz / sampleRate);
        k_ = 1.0f / resonanceQ;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    inline void processStereo(float& left, float& right) {
        float v3L = left - ic2eqL_;
        float v1L = a1_ * ic1eqL_ + a2_ * v3L;
        float v2L = ic2eqL_ + a2_ * ic1eqL_ + a3_ * v3L;
        ic1eqL_ = 2.0f * v1L - ic1eqL_;
        ic2eqL_ = 2.0f * v2L - ic2eqL_;
        left = v2L;

        float v3R = right - ic2eqR_;
        float v1R = a1_ * ic1eqR_ + a2_ * v3R;
        float v2R = ic2eqR_ + a2_ * ic1eqR_ + a3_ * v3R;
        ic1eqR_ = 2.0f * v1R - ic1eqR_;
        ic2eqR_ = 2.0f * v2R - ic2eqR_;
        right = v2R;
    }

private:
    float g_ = 0.0f, k_ = 1.414f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eqL_ = 0.0f, ic2eqL_ = 0.0f;
    float ic1eqR_ = 0.0f, ic2eqR_ = 0.0f;
};

class StereoBiquad {
public:
    void reset() { x1L_ = x2L_ = y1L_ = y2L_ = x1R_ = x2R_ = y1R_ = y2R_ = 0.0f; }

    void setPeakingEQ(float sr, float freqHz, float q, float gainDb) {
        freqHz = std::clamp(freqHz, 20.0f, sr * 0.46f);
        q = std::clamp(q, 0.20f, 15.0f);
        gainDb = std::clamp(gainDb, -18.0f, 18.0f);

        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * PI_F * freqHz / sr;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * q);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cosw0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha / A;
        setCoeffs(b0, b1, b2, a0, a1, a2);
    }

    void setHighPass(float sr, float freqHz, float q = 0.7071f) {
        freqHz = std::clamp(freqHz, 20.0f, sr * 0.45f);
        float w0 = 2.0f * PI_F * freqHz / sr;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * q);

        float b0 = (1.0f + cosw0) * 0.5f;
        float b1 = -(1.0f + cosw0);
        float b2 = (1.0f + cosw0) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;
        setCoeffs(b0, b1, b2, a0, a1, a2);
    }

    void setLowPass(float sr, float freqHz, float q = 0.7071f) {
        freqHz = std::clamp(freqHz, 40.0f, sr * 0.48f);
        float w0 = 2.0f * PI_F * freqHz / sr;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * q);

        float b0 = (1.0f - cosw0) * 0.5f;
        float b1 = 1.0f - cosw0;
        float b2 = (1.0f - cosw0) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;
        setCoeffs(b0, b1, b2, a0, a1, a2);
    }

    inline void processStereo(float& L, float& R) {
        float outL = b0_ * L + b1_ * x1L_ + b2_ * x2L_ - a1_ * y1L_ - a2_ * y2L_;
        x2L_ = x1L_; x1L_ = L; y2L_ = y1L_; y1L_ = outL;
        L = outL;

        float outR = b0_ * R + b1_ * x1R_ + b2_ * x2R_ - a1_ * y1R_ - a2_ * y2R_;
        x2R_ = x1R_; x1R_ = R; y2R_ = y1R_; y1R_ = outR;
        R = outR;
    }

private:
    void setCoeffs(float b0, float b1, float b2, float a0, float a1, float a2) {
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
        a1_ = a1 / a0; a2_ = a2 / a0;
    }
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1L_ = 0.0f, x2L_ = 0.0f, y1L_ = 0.0f, y2L_ = 0.0f;
    float x1R_ = 0.0f, x2R_ = 0.0f, y1R_ = 0.0f, y2R_ = 0.0f;
};

class MasterEqualizer {
public:
    void update(float sr, bool enabled, float lowCutHz, float highCutHz, const EqBandParams bands[5]) {
        enabled_ = enabled;
        lowCutActive_ = (lowCutHz > 22.0f);
        highCutActive_ = (highCutHz < 19500.0f);
        hpf_.setHighPass(sr, lowCutHz, 0.7071f);
        lpf_.setLowPass(sr, highCutHz, 0.7071f);
        for (int i = 0; i < 5; ++i) {
            bands_[i].setPeakingEQ(sr, bands[i].freqHz, bands[i].q, bands[i].gainDb);
        }
    }

    inline void processStereo(float& L, float& R) {
        if (!enabled_) return;
        if (lowCutActive_) hpf_.processStereo(L, R);
        if (highCutActive_) lpf_.processStereo(L, R);
        for (int i = 0; i < 5; ++i) {
            bands_[i].processStereo(L, R);
        }
    }

private:
    bool enabled_ = true;
    bool lowCutActive_ = false;
    bool highCutActive_ = false;
    StereoBiquad hpf_;
    StereoBiquad lpf_;
    StereoBiquad bands_[5];
};

class ModulationRack {
public:
    void init(float sr) {
        sr_ = sr;
        size_t maxChorusSamples = static_cast<size_t>(sr * 0.06f) + 16;
        chorusBufL_.assign(maxChorusSamples, 0.0f);
        chorusBufR_.assign(maxChorusSamples, 0.0f);
        writePos_ = 0;
    }

    void processStereo(float& L, float& R,
                       bool tremOn, float tremRate, float tremDepth,
                       bool phaserOn, float phaserRate, float phaserDepth, float phaserFb, float phaserMix,
                       bool chorusOn, float chorusRate, float chorusDepth, float chorusMix) {
        if (tremOn && tremDepth > 0.001f) {
            tremPhase_ += tremRate / sr_;
            if (tremPhase_ >= 1.0f) tremPhase_ -= 1.0f;
            float modL = 1.0f - tremDepth * 0.5f * (1.0f + std::sin(2.0f * PI_F * tremPhase_));
            float modR = 1.0f - tremDepth * 0.5f * (1.0f + std::sin(2.0f * PI_F * (tremPhase_ + 0.25f)));
            L *= modL;
            R *= modR;
        }

        if (phaserOn && phaserMix > 0.001f) {
            phaserPhase_ += phaserRate / sr_;
            if (phaserPhase_ >= 1.0f) phaserPhase_ -= 1.0f;

            float lfoL = 0.5f * (1.0f + std::sin(2.0f * PI_F * phaserPhase_));
            float lfoR = 0.5f * (1.0f + std::cos(2.0f * PI_F * phaserPhase_));

            float fMin = 220.0f, fMax = 220.0f + phaserDepth * 3400.0f;
            float freqL = fMin + (fMax - fMin) * lfoL;
            float freqR = fMin + (fMax - fMin) * lfoR;

            float tL = std::tan(PI_F * freqL / sr_);
            float aL = (1.0f - tL) / (1.0f + tL);
            float tR = std::tan(PI_F * freqR / sr_);
            float aR = (1.0f - tR) / (1.0f + tR);

            float yL = L + phaserLastL_ * std::clamp(phaserFb, 0.0f, 0.85f);
            float yR = R + phaserLastR_ * std::clamp(phaserFb, 0.0f, 0.85f);

            for (int s = 0; s < 6; ++s) {
                float outL = aL * yL + apStateL_[s];
                apStateL_[s] = yL - aL * outL;
                yL = outL;

                float outR = aR * yR + apStateR_[s];
                apStateR_[s] = yR - aR * outR;
                yR = outR;
            }
            phaserLastL_ = std::clamp(yL, -1.5f, 1.5f);
            phaserLastR_ = std::clamp(yR, -1.5f, 1.5f);

            L = L * (1.0f - phaserMix * 0.5f) + yL * (phaserMix * 0.5f);
            R = R * (1.0f - phaserMix * 0.5f) + yR * (phaserMix * 0.5f);
        }

        if (chorusOn && chorusMix > 0.001f && !chorusBufL_.empty()) {
            const size_t bufSize = chorusBufL_.size();
            chorusBufL_[writePos_] = L;
            chorusBufR_[writePos_] = R;

            chorusPhase_ += chorusRate / sr_;
            if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;

            float lfoL = std::sin(2.0f * PI_F * chorusPhase_);
            float lfoR = std::cos(2.0f * PI_F * chorusPhase_);

            float baseDelay = 0.014f * sr_;
            float modSamples = 0.008f * chorusDepth * sr_;

            auto readInterp = [&](const std::vector<float>& buf, float delaySamples) -> float {
                float readPos = static_cast<float>(writePos_) - delaySamples;
                while (readPos < 0.0f) readPos += static_cast<float>(bufSize);
                size_t i0 = static_cast<size_t>(readPos) % bufSize;
                size_t i1 = (i0 + 1) % bufSize;
                float frac = readPos - std::floor(readPos);
                return buf[i0] * (1.0f - frac) + buf[i1] * frac;
            };

            float wetL = readInterp(chorusBufL_, baseDelay + lfoL * modSamples);
            float wetR = readInterp(chorusBufR_, baseDelay + lfoR * modSamples);

            L = L * (1.0f - chorusMix * 0.5f) + wetL * chorusMix;
            R = R * (1.0f - chorusMix * 0.5f) + wetR * chorusMix;

            writePos_ = (writePos_ + 1) % bufSize;
        }
    }

private:
    float sr_ = 48000.0f;
    float tremPhase_ = 0.0f;
    float phaserPhase_ = 0.0f;
    float chorusPhase_ = 0.0f;
    float apStateL_[6] = {0.0f}, apStateR_[6] = {0.0f};
    float phaserLastL_ = 0.0f, phaserLastR_ = 0.0f;
    std::vector<float> chorusBufL_, chorusBufR_;
    size_t writePos_ = 0;
};

class StereoDelayEffect {
public:
    void init(float sr) {
        sr_ = sr;
        size_t maxFrames = static_cast<size_t>(sr * 2.1f) + 64;
        bufL_.assign(maxFrames, 0.0f);
        bufR_.assign(maxFrames, 0.0f);
        writePos_ = 0;
        lpL_ = lpR_ = 0.0f;
    }

    inline void processStereo(float& L, float& R, bool enabled, float timeMs, float feedback, float damping, float mix, bool pingPong) {
        if (!enabled || mix <= 0.001f || bufL_.empty()) return;

        const size_t sz = bufL_.size();
        size_t delayFrames = static_cast<size_t>(std::clamp(timeMs * 0.001f * sr_, 32.0f, static_cast<float>(sz - 8)));
        size_t readPos = (writePos_ + sz - delayFrames) % sz;

        float dL = bufL_[readPos];
        float dR = bufR_[readPos];

        float dampCoef = std::clamp(damping * 0.82f, 0.05f, 0.85f);
        lpL_ = dL * (1.0f - dampCoef) + lpL_ * dampCoef;
        lpR_ = dR * (1.0f - dampCoef) + lpR_ * dampCoef;

        float fb = std::clamp(feedback, 0.0f, 0.92f);
        if (pingPong) {
            bufL_[writePos_] = L + lpR_ * fb;
            bufR_[writePos_] = R * 0.5f + lpL_ * fb;
        } else {
            bufL_[writePos_] = L + lpL_ * fb;
            bufR_[writePos_] = R + lpR_ * fb;
        }

        writePos_ = (writePos_ + 1) % sz;

        L += dL * mix;
        R += dR * mix;
    }

private:
    float sr_ = 48000.0f;
    std::vector<float> bufL_, bufR_;
    size_t writePos_ = 0;
    float lpL_ = 0.0f, lpR_ = 0.0f;
};

// ============================================================================
//  ShimmerReverbEngine — Arquitectura Atmosférica FDN 8x8 + Pitch-Shifter
//  en el Lazo de Realimentación (Inspirado en ValhallaShimmer)
//  - Tiempos de tanque largos con matriz Householder 8x8 y descorrelación L/R
//  - Decay masivo (hasta 40+ segundos) y expansión Mid/Side de 180°
// ============================================================================
class ShimmerReverbEngine {
public:
    enum class PitchMode : int {
        Bypass        = 0,
        Single        = 1,
        Dual          = 2,
        SingleReverse = 3,
        DualReverse   = 4
    };
    enum class ReverbMode : int {
        Mono          = 0,
        SmallStereo   = 1,
        MediumStereo  = 2,
        BigStereo     = 3
    };
    enum class ColorMode : int {
        Bright = 0,
        Dark   = 1
    };

    void init(float sr) {
        sr_ = (sr > 8000.0f) ? sr : 48000.0f;
        const float scale = sr_ / 44100.0f;

        // 1. Pre-delay estéreo (hasta 250 ms)
        preDelayBufL_.assign(static_cast<size_t>(sr_ * 0.25f) + 32, 0.0f);
        preDelayBufR_.assign(static_cast<size_t>(sr_ * 0.25f) + 32, 0.0f);
        preWrite_ = 0;

        // 2. Difusores de entrada (8 etapas All-Pass en serie con tiempos primos distintos en L y R)
        const int inApTimesL[8] = {131, 193, 277, 389, 509, 643, 797, 953};
        const int inApTimesR[8] = {149, 211, 307, 419, 541, 677, 829, 991};
        for (int i = 0; i < 8; ++i) {
            inApL_[i].assign(static_cast<size_t>(inApTimesL[i] * scale) + 16, 0.0f);
            inApR_[i].assign(static_cast<size_t>(inApTimesR[i] * scale) + 16, 0.0f);
            inApPosL_[i] = inApPosR_[i] = 0;
        }

        // 3. Tanque FDN de 8 líneas largas (de 55 ms a 125 ms base, escalables hasta 2.8x en BigStereo)
        // Los valores de L y R son primos entre sí con amplia separación para crear una imagen estéreo enorme
        const int combBaseL[8] = {2477, 2797, 3163, 3539, 3919, 4339, 4793, 5279};
        const int combBaseR[8] = {2609, 2953, 3329, 3709, 4111, 4549, 5011, 5503};
        for (int i = 0; i < 8; ++i) {
            baseDelayL_[i] = static_cast<float>(combBaseL[i]) * scale;
            baseDelayR_[i] = static_cast<float>(combBaseR[i]) * scale;
            // Reservamos 3.5x de margen para que BigStereo + Size=1.0 + Modulación jamás trunquen el retardo
            const size_t maxLenL = static_cast<size_t>(baseDelayL_[i] * 3.5f) + 256;
            const size_t maxLenR = static_cast<size_t>(baseDelayR_[i] * 3.5f) + 256;
            combL_[i].assign(maxLenL, 0.0f);
            combR_[i].assign(maxLenR, 0.0f);
            scaledDelayL_[i] = baseDelayL_[i];
            scaledDelayR_[i] = baseDelayR_[i];
            combPosL_[i] = combPosR_[i] = 0;
            lpStateL_[i] = lpStateR_[i] = 0.0f;
            hpStateL_[i] = hpStateR_[i] = 0.0f;
        }

        // 4. Difusión tardía de salida (4 etapas largas estilo Lexicon/Valhalla para el efecto "Bloom")
        const int outApBaseL[4] = {919, 739, 571, 409};
        const int outApBaseR[4] = {977, 787, 613, 443};
        for (int i = 0; i < 4; ++i) {
            outApL_[i].assign(static_cast<size_t>(outApBaseL[i] * scale * 1.4f) + 32, 0.0f);
            outApR_[i].assign(static_cast<size_t>(outApBaseR[i] * scale * 1.4f) + 32, 0.0f);
            outApPosL_[i] = outApPosR_[i] = 0;
        }

        // 5. Pitch Shifter Granular en Cuadratura (~165 ms de ventana para máxima suavidad sin vibrato artificial)
        grainSize_ = static_cast<size_t>(sr_ * 0.165f);
        if (grainSize_ < 4096) grainSize_ = 4096;
        grainBufL_.assign(grainSize_, 0.0f);
        grainBufR_.assign(grainSize_, 0.0f);
        grainWritePos_ = 0;
        phasorUpL_ = 0.0;
        phasorUpR_ = 0.125; // Descorrelación estéreo en los granos L/R
        phasorDownL_ = 0.25;
        phasorDownR_ = 0.375;

        fbLowCut_.reset();
        fbHighCut_.reset();
        fbReturnL_ = fbReturnR_ = 0.0f;

        dcBlockerL_x1_ = dcBlockerL_y1_ = 0.0f;
        dcBlockerR_x1_ = dcBlockerR_y1_ = 0.0f;
        lfoPhase_ = 0.0f;

        setParams(mix_, shiftSt_, feedback_, diffusion_, size_,
                  lowCutHz_, highCutHz_, modRateHz_, modDepth_,
                  pitchMode_, revMode_, color_);
    }

    void setParams(float mix, float shiftSemitones, float feedback,
                   float diffusion, float size,
                   float lowCutHz, float highCutHz,
                   float modRateHz, float modDepth,
                   PitchMode pitchMode, ReverbMode revMode, ColorMode color) {
        mix_          = std::clamp(mix, 0.0f, 1.0f);
        shiftSt_      = std::clamp(shiftSemitones, -12.0f, 12.0f);
        feedback_     = std::clamp(feedback, 0.0f, 0.992f);
        diffusion_    = std::clamp(diffusion, 0.0f, 0.98f);
        size_         = std::clamp(size, 0.05f, 1.0f);
        lowCutHz_     = std::clamp(lowCutHz, 20.0f, 1000.0f);
        highCutHz_    = std::clamp(highCutHz, 1000.0f, 20000.0f);
        modRateHz_    = std::clamp(modRateHz, 0.05f, 5.0f);
        modDepth_     = std::clamp(modDepth, 0.0f, 1.0f);
        pitchMode_    = pitchMode;
        revMode_      = revMode;
        color_        = color;

        // Difusión estilo Golden Ratio (0.618 -> 0.78) para crear el ataque envolvente ("Bloom")
        apCoef_ = 0.25f + diffusion_ * 0.50f;
        pitchRatio_ = std::pow(2.0, static_cast<double>(shiftSt_) / 12.0);

        fbLowCut_.setHighPass(sr_, lowCutHz_, 0.707f);
        const float hcQ = (color_ == ColorMode::Dark) ? 0.55f : 0.707f;
        const float hcFreq = (color_ == ColorMode::Dark)
                             ? std::min(highCutHz_, 5200.0f)
                             : highCutHz_;
        fbHighCut_.setLowPass(sr_, hcFreq, hcQ);

        // Escala espacial amplia según Size y ReverbMode (sin desbordar jamás el buffer de 3.5x)
        float sizeScale = 0.45f + size_ * 1.45f; // 0.45x a 1.90x
        if (revMode_ == ReverbMode::SmallStereo)  sizeScale *= 0.55f;
        else if (revMode_ == ReverbMode::MediumStereo) sizeScale *= 1.05f;
        else if (revMode_ == ReverbMode::BigStereo)    sizeScale *= 1.60f; // Hasta 3.04x (< 3.5x reservado)

        for (int i = 0; i < 8; ++i) {
            scaledDelayL_[i] = baseDelayL_[i] * sizeScale;
            scaledDelayR_[i] = baseDelayR_[i] * sizeScale;
        }
    }

    inline void processStereo(float& L, float& R) {
        if (mix_ <= 0.001f || combL_[0].empty()) return;

        // 1. PRE-DELAY ESTÉREO (Conserva la imagen estéreo original del piano con cruce suave)
        float inStereoL = L * 0.85f + R * 0.15f;
        float inStereoR = R * 0.85f + L * 0.15f;
        if (revMode_ == ReverbMode::Mono) {
            float m = (L + R) * 0.5f;
            inStereoL = inStereoR = m;
        }

        preDelayBufL_[preWrite_] = inStereoL;
        preDelayBufR_[preWrite_] = inStereoR;
        size_t preRead = (preWrite_ + preDelayBufL_.size()
                          - static_cast<size_t>(preDelayMsSamples_)) % preDelayBufL_.size();
        preWrite_ = (preWrite_ + 1) % preDelayBufL_.size();

        float diffL = preDelayBufL_[preRead];
        float diffR = preDelayBufR_[preRead];

        // 2. RED DE DIFUSIÓN DE ENTRADA (Suaviza los ataques percusivos convirtiéndolos en una nube)
        int activeStages = std::clamp(2 + static_cast<int>(diffusion_ * 6.5f), 2, 8);
        for (int i = 0; i < activeStages; ++i) {
            float bL = inApL_[i][inApPosL_[i]];
            float xL = diffL;
            diffL = -xL * apCoef_ + bL;
            inApL_[i][inApPosL_[i]] = xL + diffL * apCoef_;
            if (++inApPosL_[i] >= inApL_[i].size()) inApPosL_[i] = 0;

            float bR = inApR_[i][inApPosR_[i]];
            float xR = diffR;
            diffR = -xR * apCoef_ + bR;
            inApR_[i][inApPosR_[i]] = xR + diffR * apCoef_;
            if (++inApPosR_[i] >= inApR_[i].size()) inApPosR_[i] = 0;
        }

        // 3. LFO MULTIFASE PARA MODULAR EL TANQUE (Crea amplitud coral y elimina resonancias estáticas)
        lfoPhase_ += modRateHz_ / sr_;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
        const float modExcursion = (8.0f + modDepth_ * 34.0f) * (sr_ / 44100.0f);

        // Curva de Decay Atmosférico: mapea feedback_ (0..0.98) a una ganancia de sostenimiento real (0.82..0.992)
        float tankDecay = 0.80f + std::pow(feedback_, 0.52f) * 0.182f;
        if (revMode_ == ReverbMode::BigStereo)    tankDecay = std::min(0.993f, tankDecay + 0.012f);
        if (revMode_ == ReverbMode::SmallStereo)  tankDecay *= 0.94f;

        const float dampCoef = (color_ == ColorMode::Dark) ? 0.34f : 0.16f;

        // 4. LECTURA DE LAS 8 LÍNEAS DEL TANQUE CON INTERPOLACIÓN FRACCIONAL
        float yL[8], yR[8];
        float matrixSumL = 0.0f, matrixSumR = 0.0f;

        for (int i = 0; i < 8; ++i) {
            const size_t szL = combL_[i].size();
            const size_t szR = combR_[i].size();

            // 8 fases distribuidas cada 45 grados (PI/4)
            float angle = 2.0f * PI_F * lfoPhase_ + static_cast<float>(i) * (PI_F * 0.25f);
            float modL = std::sin(angle) * modExcursion;
            float modR = std::cos(angle) * modExcursion;

            float dL = std::clamp(scaledDelayL_[i] + modL, 16.0f, static_cast<float>(szL - 4));
            float dR = std::clamp(scaledDelayR_[i] + modR, 16.0f, static_cast<float>(szR - 4));

            auto readInterp = [](const std::vector<float>& buf, size_t sz, size_t wpos, float delay) -> float {
                float rPos = static_cast<float>(wpos) - delay;
                if (rPos < 0.0f) rPos += static_cast<float>(sz);
                size_t i0 = static_cast<size_t>(rPos) % sz;
                size_t i1 = (i0 + 1) % sz;
                float frac = rPos - std::floor(rPos);
                return buf[i0] * (1.0f - frac) + buf[i1] * frac;
            };

            float rawL = readInterp(combL_[i], szL, combPosL_[i], dL);
            float rawR = readInterp(combR_[i], szR, combPosR_[i], dR);

            // Filtrado pasa-bajos suave por iteración (absorción del aire)
            lpStateL_[i] = rawL * (1.0f - dampCoef) + lpStateL_[i] * dampCoef;
            lpStateR_[i] = rawR * (1.0f - dampCoef) + lpStateR_[i] * dampCoef;

            // Filtro pasa-altos de 1 polo (~35 Hz) dentro de cada línea para evitar acumulación de subgraves
            float dcL = lpStateL_[i] - hpStateL_[i];
            hpStateL_[i] += dcL * 0.0045f;
            float dcR = lpStateR_[i] - hpStateR_[i];
            hpStateR_[i] += dcR * 0.0045f;

            yL[i] = dcL;
            yR[i] = dcR;
            matrixSumL += dcL;
            matrixSumR += dcR;
        }

        // Factor Householder 2/N (para N=8 es 0.25f): acopla todas las líneas entre sí sin pérdida de energía
        const float householderL = matrixSumL * 0.25f;
        const float householderR = matrixSumR * 0.25f;

        // 5. ESCRITURA EN EL TANQUE CON CRUCE ESTÉREO + RETORNO DEL SHIMMER PITCH SHIFTER
        for (int i = 0; i < 8; ++i) {
            float scatL = yL[i] - householderL;
            float scatR = yR[i] - householderR;

            // Acoplamiento cruzado L <-> R para máxima anchura tridimensional
            float fbTankL = (scatL * 0.86f + scatR * 0.14f) * tankDecay;
            float fbTankR = (scatR * 0.86f + scatL * 0.14f) * tankDecay;

            // Alternar polaridad fina de inyección para evitar cancelación de fase
            float sign = (i & 1) ? 1.0f : -1.0f;
            float nextInL = (diffL * 0.45f) + (fbReturnR_ * 0.42f * sign) + fbTankL;
            float nextInR = (diffR * 0.45f) + (fbReturnL_ * 0.42f * sign) + fbTankR;

            combL_[i][combPosL_[i]] = softSaturate(nextInL);
            combR_[i][combPosR_[i]] = softSaturate(nextInR);

            if (++combPosL_[i] >= combL_[i].size()) combPosL_[i] = 0;
            if (++combPosR_[i] >= combR_[i].size()) combPosR_[i] = 0;
        }

        // Suma estéreo descorrelacionada del tanque
        float tankOutL = (yL[0] + yL[2] + yL[4] + yL[6] - yL[1] * 0.5f + yR[3] * 0.35f) * 0.38f;
        float tankOutR = (yR[1] + yR[3] + yR[5] + yR[7] - yR[0] * 0.5f + yL[2] * 0.35f) * 0.38f;

        // 6. PITCH SHIFTER DE 4 GRANOS EN CUADRATURA DENTRO DEL LAZO DE FEEDBACK
        grainBufL_[grainWritePos_] = tankOutL + diffL * 0.30f;
        grainBufR_[grainWritePos_] = tankOutR + diffR * 0.30f;

        float shiftedL = tankOutL;
        float shiftedR = tankOutR;

        const bool isRev = (pitchMode_ == PitchMode::SingleReverse || pitchMode_ == PitchMode::DualReverse);
        if (pitchMode_ != PitchMode::Bypass && (std::abs(shiftSt_) > 0.01f || isRev)) {
            auto evalStereoGrains = [&](double phBaseL, double phBaseR, float& outGL, float& outGR) {
                outGL = 0.0f;
                outGR = 0.0f;
                const double maxSpan = static_cast<double>(grainSize_ - 16);
                for (int g = 0; g < 4; ++g) {
                    double offset = g * 0.25;
                    double phL = phBaseL + offset;
                    phL -= std::floor(phL);
                    double phR = phBaseR + offset;
                    phR -= std::floor(phR);

                    // Ganancia 0.36f compensa la pérdida de coherencia de fase entre los 4 granos superpuestos
                    float winL = 0.36f * (1.0f - std::cos(2.0f * PI_F * static_cast<float>(phL)));
                    float winR = 0.36f * (1.0f - std::cos(2.0f * PI_F * static_cast<float>(phR)));

                    double rPosL = static_cast<double>(grainWritePos_) - (phL * maxSpan);
                    while (rPosL < 0.0) rPosL += static_cast<double>(grainSize_);
                    size_t i0L = static_cast<size_t>(rPosL) % grainSize_;
                    size_t i1L = (i0L + 1) % grainSize_;
                    float fracL = static_cast<float>(rPosL - std::floor(rPosL));

                    double rPosR = static_cast<double>(grainWritePos_) - (phR * maxSpan);
                    while (rPosR < 0.0) rPosR += static_cast<double>(grainSize_);
                    size_t i0R = static_cast<size_t>(rPosR) % grainSize_;
                    size_t i1R = (i0R + 1) % grainSize_;
                    float fracR = static_cast<float>(rPosR - std::floor(rPosR));

                    outGL += (grainBufL_[i0L] * (1.0f - fracL) + grainBufL_[i1L] * fracL) * winL;
                    outGR += (grainBufR_[i0R] * (1.0f - fracR) + grainBufR_[i1R] * fracR) * winR;
                }
            };

            float upL = 0.0f, upR = 0.0f;
            evalStereoGrains(phasorUpL_, phasorUpR_, upL, upR);

            // Micro-detune estéreo (1.0018 en L y 0.9982 en R) para que el Shimmer tenga amplitud sinfónica
            double stepUpBase = (isRev ? (1.0 + pitchRatio_) : (1.0 - pitchRatio_)) / static_cast<double>(grainSize_);
            phasorUpL_ += stepUpBase * 1.0018;
            phasorUpL_ -= std::floor(phasorUpL_);
            phasorUpR_ += stepUpBase * 0.9982;
            phasorUpR_ -= std::floor(phasorUpR_);

            if (pitchMode_ == PitchMode::Dual || pitchMode_ == PitchMode::DualReverse) {
                float dnL = 0.0f, dnR = 0.0f;
                evalStereoGrains(phasorDownL_, phasorDownR_, dnL, dnR);
                // En modo Dual combinamos el intervalo principal con una quinta/octava complementaria
                double dualRatio = (pitchRatio_ > 1.05) ? (pitchRatio_ * 0.5) : (1.0 / std::max(0.25, pitchRatio_));
                double stepDownBase = (isRev ? (1.0 + dualRatio) : (1.0 - dualRatio)) / static_cast<double>(grainSize_);
                phasorDownL_ += stepDownBase * 0.9985;
                phasorDownL_ -= std::floor(phasorDownL_);
                phasorDownR_ += stepDownBase * 1.0015;
                phasorDownR_ -= std::floor(phasorDownR_);

                shiftedL = upL * 0.78f + dnL * 0.58f;
                shiftedR = upR * 0.78f + dnR * 0.58f;
            } else {
                shiftedL = upL;
                shiftedR = upR;
            }
        }

        grainWritePos_ = (grainWritePos_ + 1) % grainSize_;

        // 7. TONE STACK DENTRO DEL FEEDBACK (Moldea progresivamente cada octava regenerada)
        fbLowCut_.processStereo(shiftedL, shiftedR);
        fbHighCut_.processStereo(shiftedL, shiftedR);

        float shimmerLoopGain = 0.45f + std::pow(feedback_, 0.65f) * 0.52f;
        if (revMode_ == ReverbMode::BigStereo) shimmerLoopGain *= 1.12f;

        fbReturnL_ = softSaturate(shiftedL * shimmerLoopGain);
        fbReturnR_ = softSaturate(shiftedR * shimmerLoopGain);

        // 8. DIFUSIÓN TARDÍA DE SALIDA (Crea el efecto "Bloom" envolvente)
        float outL = tankOutL * 0.85f + shiftedL * 0.75f;
        float outR = tankOutR * 0.85f + shiftedR * 0.75f;

        const float outApGain = 0.618f; // Proporción áurea para máxima densidad suave
        for (int i = 0; i < 4; ++i) {
            float bL = outApL_[i][outApPosL_[i]];
            float inL = outL;
            outL = -inL * outApGain + bL;
            outApL_[i][outApPosL_[i]] = inL + outL * outApGain;
            if (++outApPosL_[i] >= outApL_[i].size()) outApPosL_[i] = 0;

            float bR = outApR_[i][outApPosR_[i]];
            float inR = outR;
            outR = -inR * outApGain + bR;
            outApR_[i][outApPosR_[i]] = inR + outR * outApGain;
            if (++outApPosR_[i] >= outApR_[i].size()) outApPosR_[i] = 0;
        }

        // 9. DC BLOCKER + EXPANSOR ESTÉREO MID/SIDE (Amplitud panorámica de 180 grados)
        float cleanL = outL - dcBlockerL_x1_ + 0.995f * dcBlockerL_y1_;
        dcBlockerL_x1_ = outL; dcBlockerL_y1_ = cleanL;
        float cleanR = outR - dcBlockerR_x1_ + 0.995f * dcBlockerR_y1_;
        dcBlockerR_x1_ = outR; dcBlockerR_y1_ = cleanR;

        if (revMode_ != ReverbMode::Mono) {
            float mid  = (cleanL + cleanR) * 0.5f;
            float side = (cleanL - cleanR) * 0.5f;
            float widthBoost = (revMode_ == ReverbMode::BigStereo) ? 1.72f :
                               (revMode_ == ReverbMode::MediumStereo ? 1.48f : 1.25f);
            side *= widthBoost;
            cleanL = mid + side;
            cleanR = mid - side;
        }

        // Mezcla Equi-Potencia Dry/Wet para que la señal del piano nunca pierda presencia al subir el Mix
        float dryGain = std::cos(mix_ * PI_F * 0.42f);
        float wetGain = std::sin(mix_ * PI_F * 0.50f) * 1.35f;

        L = L * dryGain + cleanL * wetGain;
        R = R * dryGain + cleanR * wetGain;
    }

    void setPreDelayMs(float ms) {
        if (preDelayBufL_.empty()) return;
        preDelayMsSamples_ = std::clamp(ms * 0.001f * sr_, 0.0f,
                                        static_cast<float>(preDelayBufL_.size() - 4));
    }

private:
    // Saturador suave sin pérdida de nivel en señales pequeñas (ganancia unitaria alrededor de 0)
    static inline float softSaturate(float x) {
        if (std::abs(x) < 0.65f) return x;
        return std::tanh(x * 0.85f) / 0.85f;
    }

    float sr_ = 48000.0f;
    std::vector<float> preDelayBufL_, preDelayBufR_;
    size_t preWrite_ = 0;
    float preDelayMsSamples_ = 0.0f;

    std::vector<float> inApL_[8], inApR_[8];
    size_t inApPosL_[8] = {0}, inApPosR_[8] = {0};

    std::vector<float> combL_[8], combR_[8];
    float baseDelayL_[8] = {0}, baseDelayR_[8] = {0};
    float scaledDelayL_[8] = {0}, scaledDelayR_[8] = {0};
    size_t combPosL_[8] = {0}, combPosR_[8] = {0};
    float lpStateL_[8] = {0}, lpStateR_[8] = {0};
    float hpStateL_[8] = {0}, hpStateR_[8] = {0};

    std::vector<float> outApL_[4], outApR_[4];
    size_t outApPosL_[4] = {0}, outApPosR_[4] = {0};

    std::vector<float> grainBufL_, grainBufR_;
    size_t grainSize_ = 7920;
    size_t grainWritePos_ = 0;
    double phasorUpL_ = 0.0, phasorUpR_ = 0.125;
    double phasorDownL_ = 0.25, phasorDownR_ = 0.375;

    StereoBiquad fbLowCut_, fbHighCut_;
    float fbReturnL_ = 0.0f, fbReturnR_ = 0.0f;

    float dcBlockerL_x1_ = 0.0f, dcBlockerL_y1_ = 0.0f;
    float dcBlockerR_x1_ = 0.0f, dcBlockerR_y1_ = 0.0f;
    float lfoPhase_ = 0.0f;

    float mix_ = 0.35f;
    float shiftSt_ = 12.0f;
    float feedback_ = 0.65f;
    float diffusion_ = 0.88f;
    float size_ = 0.72f;
    float lowCutHz_ = 140.0f;
    float highCutHz_ = 8500.0f;
    float modRateHz_ = 0.42f;
    float modDepth_ = 0.28f;
    float apCoef_ = 0.65f;
    double pitchRatio_ = 2.0;

    PitchMode pitchMode_ = PitchMode::Dual;
    ReverbMode revMode_ = ReverbMode::BigStereo;
    ColorMode color_ = ColorMode::Dark;
};

// ============================================================================
//  NordReverbEffect — Enruta automáticamente el modo 3 (AMB / SHIMMER) al
//  nuevo motor atmosférico ShimmerReverbEngine sin requerir cambios en otros
//  archivos C++ ni en la interfaz gráfica.
// ============================================================================
class NordReverbEffect {
public:
    void init(float sr) {
        sr_ = (sr > 8000.0f) ? sr : 48000.0f;
        float scale = sr_ / 44100.0f;

        const int inApTimes[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) {
            inApL_[i].assign(static_cast<size_t>(inApTimes[i] * scale) + 8, 0.0f);
            inApR_[i].assign(static_cast<size_t>((inApTimes[i] + 29) * scale) + 8, 0.0f);
            inApPosL_[i] = inApPosR_[i] = 0;
        }

        const int combBaseL[8] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
        const int combBaseR[8] = {1637, 1709, 1571, 1499, 1361, 1439, 1259, 1193};
        for (int i = 0; i < 8; ++i) {
            baseCombDelayL_[i] = static_cast<float>(combBaseL[i]) * scale * 1.45f;
            baseCombDelayR_[i] = static_cast<float>(combBaseR[i]) * scale * 1.45f;
            combL_[i].assign(static_cast<size_t>(baseCombDelayL_[i]) + 64, 0.0f);
            combR_[i].assign(static_cast<size_t>(baseCombDelayR_[i]) + 64, 0.0f);
            combPosL_[i] = combPosR_[i] = 0;
            combFltL_[i] = combFltR_[i] = 0.0f;
        }

        const int outApBase[3] = {672, 556, 441};
        for (int i = 0; i < 3; ++i) {
            apL_[i].assign(static_cast<size_t>(outApBase[i] * scale * 1.3f) + 16, 0.0f);
            apR_[i].assign(static_cast<size_t>((outApBase[i] + 37) * scale * 1.3f) + 16, 0.0f);
            apPosL_[i] = apPosR_[i] = 0;
        }

        preDelayBuf_.assign(static_cast<size_t>(sr_ * 0.25f) + 16, 0.0f);
        preWrite_ = 0;
        lfoPhase_ = 0.0f;
        dcX1L_ = dcY1L_ = dcX1R_ = dcY1R_ = 0.0f;

        shimmerEngine_.init(sr_);
    }

    inline void processStereo(float& L, float& R, bool enabled, int type, float mix, float size, float damp, float preDelayMs, float shimmerAmt) {
        if (!enabled || mix <= 0.001f || combL_[0].empty()) return;

        // Si está seleccionado el modo 3 (AMB / SHIMMER), procesamos con el motor atmosférico ShimmerReverbEngine
        // en modo BigStereo + Dual Shimmer para lograr el decay más largo y amplio de todos los reverbs
        if (type == 3) {
            shimmerEngine_.setPreDelayMs(preDelayMs);
            float highCutHz = 11500.0f - std::clamp(damp, 0.0f, 1.0f) * 7500.0f; // 4 kHz a 11.5 kHz
            float feedback  = std::clamp(0.45f + size * 0.52f, 0.45f, 0.975f);
            auto pMode = (shimmerAmt <= 0.02f)
                         ? ShimmerReverbEngine::PitchMode::Bypass
                         : (shimmerAmt > 0.35f ? ShimmerReverbEngine::PitchMode::Dual : ShimmerReverbEngine::PitchMode::Single);

            shimmerEngine_.setParams(
                mix,
                12.0f,                      // +12 semitonos (Octava Shimmer)
                feedback,                   // Decay masivo controlado por Size
                0.86f + shimmerAmt * 0.10f, // Alta difusión tipo nube
                std::max(0.65f, size),      // Tamaño de catedral
                135.0f,                     // Low Cut limpio en el feedback
                highCutHz,                  // High Cut controlado por Damping
                0.42f,                      // Mod Rate
                0.26f + shimmerAmt * 0.22f, // Mod Depth coral
                pMode,
                ShimmerReverbEngine::ReverbMode::BigStereo,
                (damp > 0.45f) ? ShimmerReverbEngine::ColorMode::Dark : ShimmerReverbEngine::ColorMode::Bright
            );
            shimmerEngine_.processStereo(L, R);
            return;
        }

        // Modos acústicos clásicos: 0 = Room, 1 = Plate, 2 = Hall
        float baseDecay = 0.68f;
        float dampScale = 1.0f;
        if (type == 0) { baseDecay = 0.62f; dampScale = 1.15f; }      // Room
        else if (type == 1) { baseDecay = 0.78f; dampScale = 0.55f; } // Plate
        else if (type == 2) { baseDecay = 0.84f; dampScale = 0.80f; } // Hall

        float fb = std::clamp(baseDecay + size * 0.13f, 0.45f, 0.96f);
        float d1 = std::clamp(damp * 0.52f * dampScale, 0.05f, 0.78f);
        float d2 = 1.0f - d1;

        float monoIn = (L + R) * 0.24f;
        size_t preSamples = static_cast<size_t>(std::clamp(preDelayMs * 0.001f * sr_, 1.0f, static_cast<float>(preDelayBuf_.size() - 4)));
        preDelayBuf_[preWrite_] = monoIn;
        size_t preRead = (preWrite_ + preDelayBuf_.size() - preSamples) % preDelayBuf_.size();
        preWrite_ = (preWrite_ + 1) % preDelayBuf_.size();
        float revIn = preDelayBuf_[preRead];

        float diffL = revIn, diffR = revIn;
        for (int i = 0; i < 4; ++i) {
            float bL = inApL_[i][inApPosL_[i]];
            float inL = diffL;
            diffL = -inL * 0.65f + bL;
            inApL_[i][inApPosL_[i]] = inL + diffL * 0.65f;
            if (++inApPosL_[i] >= inApL_[i].size()) inApPosL_[i] = 0;

            float bR = inApR_[i][inApPosR_[i]];
            float inR = diffR;
            diffR = -inR * 0.65f + bR;
            inApR_[i][inApPosR_[i]] = inR + diffR * 0.65f;
            if (++inApPosR_[i] >= inApR_[i].size()) inApPosR_[i] = 0;
        }

        lfoPhase_ += 0.45f / sr_;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
        float lfoSin = std::sin(2.0f * PI_F * lfoPhase_);
        float lfoCos = std::cos(2.0f * PI_F * lfoPhase_);

        float outL = 0.0f, outR = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const size_t szL = combL_[i].size();
            const size_t szR = combR_[i].size();

            float modOffsetL = ((i & 1) ? lfoSin : lfoCos) * 6.5f;
            float modOffsetR = ((i & 1) ? lfoCos : -lfoSin) * 6.5f;

            float dL = std::clamp(baseCombDelayL_[i] + modOffsetL, 16.0f, static_cast<float>(szL - 4));
            float dR = std::clamp(baseCombDelayR_[i] + modOffsetR, 16.0f, static_cast<float>(szR - 4));

            float rPosL = static_cast<float>(combPosL_[i]) - dL;
            if (rPosL < 0.0f) rPosL += static_cast<float>(szL);
            size_t idx0L = static_cast<size_t>(rPosL) % szL;
            size_t idx1L = (idx0L + 1) % szL;
            float fracL = rPosL - std::floor(rPosL);
            float yL = combL_[i][idx0L] * (1.0f - fracL) + combL_[i][idx1L] * fracL;

            float rPosR = static_cast<float>(combPosR_[i]) - dR;
            if (rPosR < 0.0f) rPosR += static_cast<float>(szR);
            size_t idx0R = static_cast<size_t>(rPosR) % szR;
            size_t idx1R = (idx0R + 1) % szR;
            float fracR = rPosR - std::floor(rPosR);
            float yR = combR_[i][idx0R] * (1.0f - fracR) + combR_[i][idx1R] * fracR;

            combFltL_[i] = yL * d2 + combFltL_[i] * d1;
            combFltR_[i] = yR * d2 + combFltR_[i] * d1;

            combL_[i][combPosL_[i]] = diffL + combFltL_[i] * fb;
            combR_[i][combPosR_[i]] = diffR + combFltR_[i] * fb;

            if (++combPosL_[i] >= szL) combPosL_[i] = 0;
            if (++combPosR_[i] >= szR) combPosR_[i] = 0;

            outL += yL;
            outR += yR;
        }

        outL *= 0.26f;
        outR *= 0.26f;

        for (int i = 0; i < 3; ++i) {
            float bufOutL = apL_[i][apPosL_[i]];
            float inL = outL;
            outL = -inL * 0.6f + bufOutL;
            apL_[i][apPosL_[i]] = inL + bufOutL * 0.6f;
            if (++apPosL_[i] >= apL_[i].size()) apPosL_[i] = 0;

            float bufOutR = apR_[i][apPosR_[i]];
            float inR = outR;
            outR = -inR * 0.6f + bufOutR;
            apR_[i][apPosR_[i]] = inR + bufOutR * 0.6f;
            if (++apPosR_[i] >= apR_[i].size()) apPosR_[i] = 0;
        }

        float cleanL = outL - dcX1L_ + 0.995f * dcY1L_;
        dcX1L_ = outL; dcY1L_ = cleanL;
        float cleanR = outR - dcX1R_ + 0.995f * dcY1R_;
        dcX1R_ = outR; dcY1R_ = cleanR;

        L = L * (1.0f - mix * 0.28f) + cleanL * (mix * 0.55f);
        R = R * (1.0f - mix * 0.28f) + cleanR * (mix * 0.55f);
    }

private:
    float sr_ = 48000.0f;
    std::vector<float> inApL_[4], inApR_[4];
    size_t inApPosL_[4] = {0}, inApPosR_[4] = {0};

    std::vector<float> combL_[8], combR_[8];
    float baseCombDelayL_[8] = {0.0f}, baseCombDelayR_[8] = {0.0f};
    size_t combPosL_[8] = {0}, combPosR_[8] = {0};
    float combFltL_[8] = {0.0f}, combFltR_[8] = {0.0f};

    std::vector<float> apL_[3], apR_[3];
    size_t apPosL_[3] = {0}, apPosR_[3] = {0};
    std::vector<float> preDelayBuf_;
    size_t preWrite_ = 0;

    float lfoPhase_ = 0.0f;
    float dcX1L_ = 0.0f, dcY1L_ = 0.0f, dcX1R_ = 0.0f, dcY1R_ = 0.0f;

    ShimmerReverbEngine shimmerEngine_;
};

} // namespace NordDSP