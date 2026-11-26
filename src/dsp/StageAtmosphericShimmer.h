// Sonus Live Stage — Shimmer Atmosphere v5 (engine revision 5.0).
// Included *inside* namespace SonusDSP in StageDspEffects.h, after StereoBiquad.
//
// v5 is a redesign of the v4 tank after an offline analysis of its tail (v4 measured a
// 68 s default RT60 with 250–500 Hz +10 dB over 1 kHz after 3 s, 150–460 ms tank lines
// with no in-loop diffusion, and a fixed-period two-head pitch shifter):
//
//   input -> pre-delay -> early reflections (L/R cross-fed)
//         -> 4 (+2 BLOOM) input all-pass diffusers
//         -> 16-line Householder FDN, every line = modulated delay (Hermite read)
//            + in-loop all-pass (echo density) + Jot absorption filter
//              (exact RT60 at low / mid / high: low shelf + first-order HF loss)
//   shimmer: anti-aliased two-head Hann shifters per voice with randomised grain
//            offsets (no periodic warble). A voice at unison or at 0 level is off.
//            Octave intervals regenerate fully; other intervals (e.g. a fifth)
//            regenerate weakly, so they do not stack into clusters.
//   output: late field with a coherent centre (mono compatible) + early reflections
//           + transposed voices, M/S width, equal-power dry/wet.
//
// DECAY maps to RT60 1.2 s … 40 s (ambient range); FREEZE holds the tank indefinitely.
// The soft limiters are safety nets well above the normal operating level: the tank is
// linear (no level-dependent harmonics).
// All delay memory is allocated in init(); no allocation, locks or I/O in processStereo().
#pragma once
class StageAtmosphericShimmer {
public:
    using PitchMode = ShimmerReverbEngine::PitchMode;
    using ReverbMode = ShimmerReverbEngine::ReverbMode;
    using ColorMode = ShimmerReverbEngine::ColorMode;

    /** RT60 (seconds) of the mid band for a DECAY value 0..0.992 (shared with the UI / tail timer). */
    static float decaySeconds(float feedback) {
        const float f = std::clamp(feedback / .992f, 0.f, 1.f);
        return kRtMin * std::pow(kRtMax / kRtMin, std::pow(f, 1.25f));
    }

    void init(float sr) {
        sr_ = std::clamp(sr, 8000.f, 192000.f);
        // Mutually prime-ish lengths, 31–79 ms: dense, non-periodic modes.
        constexpr float lengthsMs[16] = {31.7f, 37.3f, 41.9f, 47.3f, 53.9f, 59.9f, 67.1f, 73.7f,
                                         33.1f, 38.9f, 44.3f, 50.9f, 56.3f, 63.7f, 70.9f, 79.3f};
        constexpr float apMs[16] = {4.3f, 5.9f, 7.1f, 3.7f, 6.7f, 4.9f, 8.3f, 5.3f,
                                    4.7f, 6.1f, 7.7f, 3.9f, 5.1f, 8.9f, 4.1f, 6.3f};
        for (int i = 0; i < 16; ++i) {
            baseDelay_[i] = lengthsMs[i] * sr_ * .001f;
            // Largest SIZE x BIG ST scale is 1.92; leave room for modulation and Hermite taps.
            lines_[i].assign(size_t(baseDelay_[i] * 2.05f) + size_t(sr_ * .002f) + 64, 0.f);
            pos_[i] = 0;
            loopAp_[i].assign(size_t(apMs[i] * sr_ * .001f) + 1, 0.f);
            loopApPos_[i] = 0;
            lfState_[i] = hfState_[i] = 0.f;
            delay_[i] = delayTarget_[i] = baseDelay_[i];
            gains_[i] = gainsTarget_[i] = .9f;
            lowK_[i] = lowKTarget_[i] = 0.f;
            hfA_[i] = hfATarget_[i] = 0.f;
            modC_[i] = std::cos(float(i) * .793f);
            modS_[i] = std::sin(float(i) * .793f);
        }
        preL_.assign(size_t(sr_ * .34f) + 16, 0.f);
        preR_.assign(preL_.size(), 0.f);
        static constexpr float offsets[kApStages] = {3.23f, 5.97f, 9.17f, 13.79f, 19.31f, 27.11f};
        for (int i = 0; i < kApStages; ++i) {
            apL_[i].assign(size_t(offsets[i] * sr_ * .001f) + 4, 0.f);
            apR_[i].assign(size_t((offsets[i] + .73f) * sr_ * .001f) + 4, 0.f);
            apPosL_[i] = apPosR_[i] = 0;
        }
        // Longest read: 210 ms grain x 3 (±24 st sweep) x 1.08 jitter + 4 ms.
        grainL_.assign(size_t(sr_ * .78f) + 128, 0.f);
        grainR_.assign(grainL_.size(), 0.f);
        grainWrite_ = 0; preWrite_ = 0;
        for (auto& v : voice_) for (int c = 0; c < 2; ++c) { v.phase[c] = 0.; v.prev[c][0] = v.prev[c][1] = 0.f; v.off[c][0] = v.off[c][1] = 0.f; }
        voice_[0].phase[0] = .15; voice_[0].phase[1] = .29; voice_[1].phase[0] = .39; voice_[1].phase[1] = .53;
        // Power-complementary (sine) grain windows: sin²(πp) + sin²(π(p+½)) = 1. The two
        // heads read diffuse, mutually uncorrelated tank output, so constant POWER (not
        // amplitude, as with Hann) is what keeps the transposed voice free of the −3 dB
        // dip per crossfade — a tremolo at twice the grain rate (~16 Hz, heard as roughness).
        for (size_t i = 0; i < window_.size(); ++i)
            window_[i] = std::sin(PI_F * (float(i) + .5f) / float(window_.size()));
        grainSpan_ = grainSpanTarget_ = sr_ * .12f;
        grainCoeff_ = 1.f - std::exp(-1.f / (sr_ * .06f));
        ampCoeff_ = 1.f - std::exp(-1.f / (sr_ * .02f));
        gainCoeff_ = 1.f - std::exp(-1.f / (sr_ * .04f));
        freezeCoeff_ = 1.f - std::exp(-1.f / (sr_ * .08f));
        lfCoeff_ = 1.f - std::exp(-2.f * PI_F * kLowCorner / sr_);
        pitchToneL_ = pitchToneR_ = 0.f;
        denormSign_ = 1.f;
        freezeAmt_ = freezeTarget_ = 0.f;
        rng_ = 0x9e3779b9u;
        for (int k = 0; k < kEarlyTaps; ++k) erPos_[k][0] = erPos_[k][1] = erTarget_[k][0] = erTarget_[k][1] = 1.f;
        wetLow_.reset(); wetHigh_.reset(); pitchLow_.reset(); pitchHigh_.reset(); aa1_.reset(); aa2_.reset();
        setParams(1.f, 12.f, .70f, .85f, .80f, 140.f, 11000.f, .23f, .15f,
                  PitchMode::Dual, ReverbMode::BigStereo, ColorMode::Bright);
        setMultiPitch(7.f, .30f, 125.f, .5f, .8f, .7f);
        setPitchReturnAmount(.5f);
        setPreDelayMs(60.f);
        setDamping(.3f);
        setAtmosphere(.6f, false);
        snapToTargets();
    }

    void setParams(float mix, float pitch, float feedback, float diffusion, float size,
                   float lowCut, float highCut, float modRate, float modDepth,
                   PitchMode pitchMode, ReverbMode reverbMode, ColorMode color) {
        mix_ = std::clamp(mix, 0.f, 1.f);
        semisA_ = std::clamp(pitch, -24.f, 24.f);
        feedback_ = std::clamp(feedback, 0.f, .992f);
        diffusion_ = std::clamp(diffusion, 0.f, 1.f);
        size_ = std::clamp(size, .05f, 1.f);
        modRate_ = std::clamp(modRate, .05f, 5.f);
        modDepth_ = std::clamp(modDepth, 0.f, 1.f);
        pitchMode_ = pitchMode; reverbMode_ = reverbMode; color_ = color;
        highCut_ = std::clamp(highCut, 600.f, sr_ * .45f);
        const float lf = std::clamp(lowCut, 20.f, 1000.f);
        wetLow_.setHighPass(sr_, lf, .707f);
        wetHigh_.setLowPass(sr_, highCut_, .707f);
        pitchLow_.setHighPass(sr_, std::max(lf, 120.f), .707f);
        pitchHigh_.setLowPass(sr_, highCut_, .707f);
        float modeScale = 1.f;
        switch (reverbMode_) {
            case ReverbMode::SmallStereo: modeScale = .75f; modeWidth_ = .85f; break;
            case ReverbMode::MediumStereo: modeScale = 1.f; modeWidth_ = 1.f; break;
            case ReverbMode::BigStereo: modeScale = 1.2f; modeWidth_ = 1.12f; break;
            default: modeScale = 1.f; modeWidth_ = 1.f; break;
        }
        updateWidth();
        sizeScale_ = (.6f + size_) * modeScale;              // 0.45 … 1.92
        for (int i = 0; i < 16; ++i) {
            const float w = 2.f * PI_F * modRate_ * (.8f + .45f * float(i) / 15.f) / sr_;
            modCr_[i] = std::cos(w); modSr_[i] = std::sin(w);
        }
        const float erScale = (.45f + size_ * .85f) * modeScale * .9f;
        static constexpr float tapsL[kEarlyTaps] = {7.1f, 11.3f, 17.9f, 23.7f, 31.1f, 41.3f, 53.9f, 67.7f};
        static constexpr float tapsR[kEarlyTaps] = {8.9f, 13.7f, 19.3f, 27.1f, 35.9f, 45.7f, 57.1f, 71.9f};
        for (int k = 0; k < kEarlyTaps; ++k) {
            erTarget_[k][0] = std::max(8.f, tapsL[k] * erScale * sr_ * .001f);
            erTarget_[k][1] = std::max(8.f, tapsR[k] * erScale * sr_ * .001f);
        }
        const float md = maxModSamples();
        for (int i = 0; i < 16; ++i)
            delayTarget_[i] = std::clamp(baseDelay_[i] * sizeScale_, 64.f, float(lines_[i].size()) - 2.f * md - 8.f);
        loopApGainTarget_ = .30f + .30f * diffusion_;
        apGainTarget_ = .28f + .47f * diffusion_;
        updateDecay();
        updatePitchVoices();
    }
    void setPreDelayMs(float ms) {
        const float room = float(preL_.size()) - sr_ * .095f - 8.f;
        preSamples_ = size_t(std::clamp(ms * .001f * sr_, 1.f, std::max(1.f, room)));
    }
    void setPitchReturnAmount(float x) { targetGainPitch_ = std::clamp(x, 0.f, 1.f); }
    void setDamping(float d) { damping_ = std::clamp(d, 0.f, 1.f); updateDecay(); }
    void setMultiPitch(float bSemis, float bGain, float grainMs, float tone, float aGain, float width) {
        semisB_ = std::clamp(bSemis, -24.f, 24.f);
        gainA_ = std::clamp(aGain, 0.f, 1.f); gainB_ = std::clamp(bGain, 0.f, 1.f);
        grainSpanTarget_ = sr_ * std::clamp(grainMs, 45.f, 210.f) * .001f;
        width_ = std::clamp(width * 2.f, 0.f, 2.f);
        updateWidth();
        const float f = 300.f * std::pow(55.f, std::clamp(tone, 0.f, 1.f));
        toneAlpha_ = 1.f - std::exp(-2.f * PI_F * std::min(f, sr_ * .45f) / sr_);
        updatePitchVoices();
    }
    void setAtmosphere(float bloom, bool freeze) {
        bloom_ = std::clamp(bloom, 0.f, 1.f);
        freezeTarget_ = freeze ? 1.f : 0.f;
    }

    inline void processStereo(float& left, float& right) {
        if (preL_.empty()) return;
        const float dryL = left, dryR = right;
        // ---- control smoothing: parameter moves never click -----------------------
        constexpr float kMaxSlew = .05f;
        for (int i = 0; i < 16; ++i) {
            float d = delayTarget_[i] - delay_[i];
            d = d > kMaxSlew ? kMaxSlew : (d < -kMaxSlew ? -kMaxSlew : d);
            delay_[i] += d;
            gains_[i] += (gainsTarget_[i] - gains_[i]) * gainCoeff_;
            lowK_[i] += (lowKTarget_[i] - lowK_[i]) * gainCoeff_;
            hfA_[i] += (hfATarget_[i] - hfA_[i]) * gainCoeff_;
        }
        for (int k = 0; k < kEarlyTaps; ++k) for (int c = 0; c < 2; ++c) {
            float d = erTarget_[k][c] - erPos_[k][c];
            d = d > kMaxSlew ? kMaxSlew : (d < -kMaxSlew ? -kMaxSlew : d);
            erPos_[k][c] += d;
        }
        injectGain_ += (injectTarget_ - injectGain_) * gainCoeff_;
        regenGain_ += (regenTarget_ - regenGain_) * gainCoeff_;
        apGain_ += (apGainTarget_ - apGain_) * gainCoeff_;
        loopApGain_ += (loopApGainTarget_ - loopApGain_) * gainCoeff_;
        bloomS_ += (bloom_ - bloomS_) * gainCoeff_;
        freezeAmt_ += (freezeTarget_ - freezeAmt_) * freezeCoeff_;
        for (auto& v : voice_) {
            v.ratio += (v.ratioTarget - v.ratio) * ampCoeff_;
            v.out += (v.outTarget - v.out) * ampCoeff_;
            v.regen += (v.regenTarget - v.regen) * ampCoeff_;
        }
        grainSpan_ += (grainSpanTarget_ - grainSpan_) * grainCoeff_;
        gainPitch_ += (targetGainPitch_ - gainPitch_) * ampCoeff_;
        const float live = 1.f - freezeAmt_;

        // ---- pre-delay + early reflections ----------------------------------------
        const size_t psize = preL_.size();
        preL_[preWrite_] = left; preR_[preWrite_] = right;
        const size_t read = (preWrite_ + psize - preSamples_) % psize;
        float inL = preL_[read] * live;
        float inR = preR_[read] * live;
        float erL = 0.f, erR = 0.f;
        {
            const bool mono = reverbMode_ == ReverbMode::Mono;
            float g = .62f;
            for (int k = 0; k < kEarlyTaps; ++k) {
                const float sgn = (k & 1) ? -1.f : 1.f;
                const float aL = readLine(preL_, preWrite_, float(preSamples_) + erPos_[k][0]);
                const float bR = readLine(preR_, preWrite_, float(preSamples_) + erPos_[k][1]);
                const float aR = readLine(preR_, preWrite_, float(preSamples_) + erPos_[k][0]);
                const float bL = readLine(preL_, preWrite_, float(preSamples_) + erPos_[k][1]);
                if (mono) { const float m = .5f * (aL + aR + bL + bR); erL += g * sgn * m; erR += g * sgn * m; }
                else { erL += g * sgn * (.78f * aL + .22f * aR); erR += g * sgn * (.78f * bR + .22f * bL); }
                g *= .86f;
            }
            erL *= live; erR *= live;
        }
        if (++preWrite_ >= psize) preWrite_ = 0;
        if (reverbMode_ == ReverbMode::Mono) inL = inR = .5f * (inL + inR);
        denormSign_ = -denormSign_;
        const float tiny = 1.e-18f * denormSign_;
        inL += tiny; inR -= tiny;
        for (int s = 0; s < 4; ++s) {
            inL = allpass(inL, apL_[s], apPosL_[s], apGain_);
            inR = allpass(inR, apR_[s], apPosR_[s], apGain_);
        }
        float denseL = inL, denseR = inR;
        for (int s = 4; s < kApStages; ++s) {
            denseL = allpass(denseL, apL_[s], apPosL_[s], apGain_);
            denseR = allpass(denseR, apR_[s], apPosR_[s], apGain_);
        }
        const float bl = bloomS_;
        const float tankInL = inL + (denseL - inL) * bl;
        const float tankInR = inR + (denseR - inR) * bl;

        // ---- FDN read: modulated delay -> in-loop all-pass -> Jot absorption ----------
        const float md = maxModSamples() * modDepth_;
        const float shelf = live;           // FREEZE: no absorption at any frequency
        float taps[16]; float sum = 0.f;
        for (int i = 0; i < 16; ++i) {
            const float nc = modC_[i] * modCr_[i] - modS_[i] * modSr_[i];
            const float ns = modS_[i] * modCr_[i] + modC_[i] * modSr_[i];
            const float k = 1.5f - .5f * (nc * nc + ns * ns);
            modC_[i] = nc * k; modS_[i] = ns * k;
            float x = readHermite(lines_[i], pos_[i], delay_[i] + md * (1.f + modS_[i]));
            x = allpass(x, loopAp_[i], loopApPos_[i], loopApGain_);
            // Low shelf: DC gain = gLow/gMid (bass decays a bit faster: no mud).
            lfState_[i] += lfCoeff_ * (x - lfState_[i]);
            x += lowK_[i] * shelf * lfState_[i];
            // First-order absorption: unity at DC, gHigh/gMid at Nyquist.
            const float a = hfA_[i] * shelf;
            hfState_[i] = (1.f - a) * x + a * hfState_[i];
            taps[i] = hfState_[i];
            sum += taps[i];
        }
        float wetL = 0.f, wetR = 0.f;
        for (int i = 0; i < 8; ++i) {
            wetL += (i % 2 ? -1.f : 1.f) * taps[i];
            wetR += (i % 2 ? 1.f : -1.f) * taps[i + 8];
        }
        wetL *= .25f; wetR *= .25f;

        // ---- shimmer voices ----------------------------------------------------------
        // Anti-alias before transposing up: nothing above (Nyquist / ratio) is read faster.
        float gL = wetL, gR = wetR;
        aa1_.processStereo(gL, gR);
        aa2_.processStereo(gL, gR);
        grainL_[grainWrite_] = gL;
        grainR_[grainWrite_] = gR;
        float outPL = 0.f, outPR = 0.f, regPL = 0.f, regPR = 0.f;
        if (pitchMode_ != PitchMode::Bypass) {
            const bool rev = pitchMode_ == PitchMode::SingleReverse || pitchMode_ == PitchMode::DualReverse;
            for (int vi = 0; vi < 2; ++vi) {
                Voice& v = voice_[vi];
                if (v.out < 1.e-4f && v.regen < 1.e-4f) continue;   // unison or 0 level: off
                const float sL = grainRead(grainL_, v, 0, rev);
                const float sR = grainRead(grainR_, v, 1, rev);
                outPL += v.out * sL; outPR += v.out * sR;
                regPL += v.regen * sL; regPR += v.regen * sR;
            }
        }
        if (++grainWrite_ >= grainL_.size()) grainWrite_ = 0;
        // Band-limit and tone the transposed voices (both the audible and the re-injected part).
        pitchLow_.processStereo(outPL, outPR);
        pitchHigh_.processStereo(outPL, outPR);
        pitchToneL_ += toneAlpha_ * (outPL - pitchToneL_);
        pitchToneR_ += toneAlpha_ * (outPR - pitchToneR_);
        outPL = pitchToneL_; outPR = pitchToneR_;
        pitchRegLowL_ += pitchRegCoeff_ * (regPL - pitchRegLowL_);
        pitchRegLowR_ += pitchRegCoeff_ * (regPR - pitchRegLowR_);
        regPL = regPL - pitchRegLowL_; regPR = regPR - pitchRegLowR_;   // no sub build-up in the loop
        pitchRegToneL_ += toneAlpha_ * (regPL - pitchRegToneL_);
        pitchRegToneR_ += toneAlpha_ * (regPR - pitchRegToneR_);
        regPL = pitchRegToneL_; regPR = pitchRegToneR_;

        // ---- write the tank ------------------------------------------------------------
        const float pitchInject = gainPitch_ * regenGain_ * live;
        const float orth = 2.f / 16.f * sum;
        for (int i = 0; i < 16; ++i) {
            const float matrix = orth - taps[i];      // Householder: norm preserving
            const float sampleIn = (i < 8 ? tankInL : tankInR) * injectGain_ + (i < 8 ? regPL : regPR) * pitchInject;
            const float g = gains_[i] + (kHoldGain - gains_[i]) * freezeAmt_;
            const float mixed = loopSafety(g * matrix + sampleIn) + tiny;
            lines_[i][pos_[i]] = std::isfinite(mixed) ? mixed : 0.f;
            if (++pos_[i] >= lines_[i].size()) pos_[i] = 0;
        }
        // ---- wet return -------------------------------------------------------------------
        const float common = sum * kCommonScale;
        const float lateL = wetL * kLateSide + common * kLateCentre;
        const float lateR = wetR * kLateSide + common * kLateCentre;
        const float earlyGain = kEarly * (1.f - .85f * bl);  // BLOOM trades the early field for diffusion
        float returnL = lateL + erL * earlyGain + outPL * gainPitch_ * kPitchOut;
        float returnR = lateR + erR * earlyGain + outPR * gainPitch_ * kPitchOut;
        wetLow_.processStereo(returnL, returnR);
        wetHigh_.processStereo(returnL, returnR);
        const float mid = .5f * (returnL + returnR), side = .5f * (returnL - returnR);
        returnL = mid + side * widthEff_; returnR = mid - side * widthEff_;
        const float dryGain = std::cos(mix_ * PI_F * .5f);
        const float wetGain = std::sin(mix_ * PI_F * .5f);
        float outL = dryL * dryGain + outSafety(returnL) * wetGain;
        float outR = dryR * dryGain + outSafety(returnR) * wetGain;
        if (!std::isfinite(outL) || !std::isfinite(outR)) { outL = dryL; outR = dryR; }
        left = outL; right = outR;
    }

private:
    struct Voice {
        double phase[2]{};            // per channel, 0..1 (head 2 = phase + .5)
        float prev[2][2]{};           // previous head positions (wrap detection)
        float off[2][2]{};            // per head random extra delay for the current grain
        double ratio = 1., ratioTarget = 1.;
        float out = 0.f, outTarget = 0.f, regen = 0.f, regenTarget = 0.f;
    };
    static constexpr int kApStages = 6;
    static constexpr int kEarlyTaps = 8;
    static constexpr float kRtMin = 1.2f, kRtMax = 40.f;   // DECAY range (seconds, mid band)
    static constexpr float kWetTarget = 1.10f;  // wet level matched to the Hall at equal LEVEL (pink noise)
    static constexpr float kEarly = .11f;    // early reflections: same ratio to the late field (kWetTarget) as v4
    static constexpr float kPitchOut = .80f;
    static constexpr float kPitchRegen = .9f;
    static constexpr float kHoldGain = .99998f;
    static constexpr float kLowCorner = 220.f;  // Hz, low-shelf corner of the absorption
    static constexpr float kLowRt = .75f;       // RT60 low / mid
    static constexpr float kLateSide = .72f;
    static constexpr float kLateCentre = .70f;
    static constexpr float kCommonScale = .18f;
    static constexpr float kNonOctaveRegen = .3f; // fifths etc. regenerate weakly (no stacking)
    static constexpr float kGrainJitter = .08f;   // random extra delay per grain (fraction of grain)

    static float knee(float x, float th, float ceiling) {
        const float a = std::abs(x);
        if (a <= th) return x;
        return std::copysign(th + (ceiling - th) * std::tanh((a - th) / (ceiling - th)), x);
    }
    // Safety only: far above the level the tank reaches with full-scale input.
    static float loopSafety(float x) { return knee(x, 3.f, 6.f); }
    static float outSafety(float x) { return knee(x, 1.6f, 2.4f); }
    static float allpass(float x, std::vector<float>& b, size_t& i, float g) {
        const float z = b[i]; const float y = z - g * x; b[i] = x + g * y;
        if (++i >= b.size()) i = 0;
        return y;
    }
    static float readLine(const std::vector<float>& buf, size_t write, float delay) {
        const float n = float(buf.size()); float index = float(write) - delay;
        if (index < 0.f) index += n;
        if (index >= n) index -= n;
        const size_t i = size_t(index), j = i + 1 >= buf.size() ? 0 : i + 1;
        const float frac = index - float(i);
        return buf[i] * (1.f - frac) + buf[j] * frac;
    }
    static float readHermite(const std::vector<float>& b, size_t write, float delay) {
        const int n = int(b.size());
        float index = float(write) - delay;
        while (index < 0.f) index += float(n);
        int i1 = int(index); const float f = index - float(i1);
        if (i1 >= n) i1 -= n;
        const int i0 = i1 - 1 < 0 ? n - 1 : i1 - 1;
        int i2 = i1 + 1; if (i2 >= n) i2 -= n;
        int i3 = i2 + 1; if (i3 >= n) i3 -= n;
        const float y0 = b[i0], y1 = b[i1], y2 = b[i2], y3 = b[i3];
        const float c1 = .5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - .5f * y3;
        const float c3 = .5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float random01() { rng_ = rng_ * 1664525u + 1013904223u; return float(rng_ >> 8) * (1.f / 16777216.f); }
    // Two heads half a grain apart with power-complementary windows (see init). Each new
    // grain starts at a small random extra delay, chosen while its window is at zero, so
    // successive crossfades are not periodic.
    float grainRead(const std::vector<float>& b, Voice& v, int ch, bool reverse) {
        // The read sweep scales with |1 − ratio|, so a head crossfades once per GRAIN
        // period for every interval: +24 st no longer crossfades 3× faster than +12 st
        // (a 50 Hz rough modulation instead of a gentle ~10 Hz one).
        const double sweep = reverse ? 1. + v.ratio : std::abs(1. - v.ratio);
        const float span = grainSpan_ * float(std::clamp(sweep, .5, 3.));
        const float delta = float((reverse ? 1. + v.ratio : 1. - v.ratio) / double(span));
        const float minimum = sr_ * .004f;
        const float pos[2] = {float(v.phase[ch]), std::fmod(float(v.phase[ch]) + .5f, 1.f)};
        float acc = 0.f;
        for (int h = 0; h < 2; ++h) {
            // A head wraps when its position jumps across the 0/1 boundary.
            if (std::abs(pos[h] - v.prev[ch][h]) > .5f) v.off[ch][h] = random01() * kGrainJitter * span;
            v.prev[ch][h] = pos[h];
            const float w = window_[std::min(1023, int(pos[h] * 1024.f))];
            acc += w * readHermite(b, grainWrite_, minimum + v.off[ch][h] + span * pos[h]);
        }
        v.phase[ch] += delta; v.phase[ch] -= std::floor(v.phase[ch]);
        return acc;
    }
    float maxModSamples() const { return sr_ * .0009f; }   // MOD DEPTH 100 % = ±0.45 ms
    void updateWidth() { widthEff_ = std::clamp(width_ * modeWidth_, 0.f, 2.2f); }
    // Jot absorption per line: exact RT60 at DC (low), mid band and Nyquist (high).
    void updateDecay() {
        const float rtMid = decaySeconds(feedback_);
        const float hfRatio = std::clamp((color_ == ColorMode::Dark ? .38f : .70f) * (1.f - .80f * damping_), .06f, 1.f);
        const float rtLow = rtMid * kLowRt, rtHigh = rtMid * hfRatio;
        float gSum = 0.f;
        for (int i = 0; i < 16; ++i) {
            const float loop = delayTarget_[i] + float(loopAp_[i].size());   // whole circulation
            const float gMid = std::pow(10.f, -3.f * loop / (sr_ * rtMid));
            const float gLow = std::pow(10.f, -3.f * loop / (sr_ * rtLow));
            const float gHigh = std::pow(10.f, -3.f * loop / (sr_ * rtHigh));
            gainsTarget_[i] = gMid;
            lowKTarget_[i] = gLow / gMid - 1.f;
            const float r = std::clamp(gHigh / gMid, .01f, 1.f);
            hfATarget_[i] = (1.f - r) / (1.f + r);
            gSum += gMid;
        }
        const float gMean = gSum * (1.f / 16.f);
        const float oneMinusG2 = std::max(.0004f, 1.f - gMean * gMean);
        injectTarget_ = std::clamp(kWetTarget * std::sqrt(2.f * oneMinusG2), .03f, 1.2f);
        regenTarget_ = kPitchRegen * std::sqrt(2.f * oneMinusG2);
    }
    void updatePitchVoices() {
        const bool dual = pitchMode_ == PitchMode::Dual || pitchMode_ == PitchMode::DualReverse;
        const float semis[2] = {semisA_, semisB_};
        const float level[2] = {gainA_, dual ? gainB_ : 0.f};
        const float norm = 1.f / std::max(1.f, level[0] + level[1]);
        double maxRatio = 1.;
        for (int vi = 0; vi < 2; ++vi) {
            // A voice at unison adds nothing but a comb filter: it is switched off.
            const bool active = pitchMode_ != PitchMode::Bypass && level[vi] > .001f && std::abs(semis[vi]) > .05f;
            const float octaveDist = std::abs(semis[vi] / 12.f - std::round(semis[vi] / 12.f));
            const float regenWeight = octaveDist < .01f ? 1.f : kNonOctaveRegen;
            voice_[vi].ratioTarget = std::pow(2., double(semis[vi]) / 12.);
            voice_[vi].outTarget = active ? level[vi] * norm : 0.f;
            voice_[vi].regenTarget = active ? level[vi] * norm * regenWeight : 0.f;
            if (active) maxRatio = std::max(maxRatio, voice_[vi].ratioTarget);
        }
        // Anti-alias corner for the fastest read head (and never above HIGH CUT).
        const float aa = std::clamp(float(.45 * sr_ / maxRatio), 1500.f, std::min(highCut_, sr_ * .45f));
        aa1_.setLowPass(sr_, aa, .5412f);    // 4th-order Butterworth (two sections)
        aa2_.setLowPass(sr_, aa, 1.3066f);
        pitchRegCoeff_ = 1.f - std::exp(-2.f * PI_F * 150.f / sr_);
    }
    void snapToTargets() {
        for (int i = 0; i < 16; ++i) {
            delay_[i] = delayTarget_[i]; gains_[i] = gainsTarget_[i]; lowK_[i] = lowKTarget_[i]; hfA_[i] = hfATarget_[i];
            lfState_[i] = hfState_[i] = 0.f;
        }
        for (int k = 0; k < kEarlyTaps; ++k) for (int c = 0; c < 2; ++c) erPos_[k][c] = erTarget_[k][c];
        injectGain_ = injectTarget_; regenGain_ = regenTarget_; apGain_ = apGainTarget_; loopApGain_ = loopApGainTarget_;
        bloomS_ = bloom_; freezeAmt_ = freezeTarget_; gainPitch_ = targetGainPitch_; grainSpan_ = grainSpanTarget_;
        for (auto& v : voice_) { v.ratio = v.ratioTarget; v.out = v.outTarget; v.regen = v.regenTarget; }
    }

    float sr_ = 48000.f;
    std::array<std::vector<float>, 16> lines_{}, loopAp_{};
    std::array<float, 16> baseDelay_{}, delay_{}, delayTarget_{}, gains_{}, gainsTarget_{};
    std::array<float, 16> lowK_{}, lowKTarget_{}, hfA_{}, hfATarget_{}, lfState_{}, hfState_{};
    std::array<float, 16> modC_{}, modS_{}, modCr_{}, modSr_{};
    std::array<size_t, 16> pos_{}, loopApPos_{};
    std::vector<float> preL_, preR_, grainL_, grainR_;
    std::array<std::vector<float>, kApStages> apL_{}, apR_{};
    std::array<size_t, kApStages> apPosL_{}, apPosR_{};
    std::array<float, 1024> window_{};
    float erPos_[kEarlyTaps][2]{}, erTarget_[kEarlyTaps][2]{};
    size_t preWrite_ = 0, grainWrite_ = 0, preSamples_ = 1;
    Voice voice_[2]{};
    float semisA_ = 12.f, semisB_ = 7.f;
    float grainSpan_ = 6000.f, grainSpanTarget_ = 6000.f, grainCoeff_ = .001f;
    float gainPitch_ = 0.f, targetGainPitch_ = 0.f, ampCoeff_ = .001f, gainCoeff_ = .001f, freezeCoeff_ = .001f, lfCoeff_ = .02f;
    float gainA_ = 1.f, gainB_ = .3f, width_ = 1.f, modeWidth_ = 1.f, widthEff_ = 1.f;
    float pitchToneL_ = 0.f, pitchToneR_ = 0.f, toneAlpha_ = .5f;
    float pitchRegLowL_ = 0.f, pitchRegLowR_ = 0.f, pitchRegToneL_ = 0.f, pitchRegToneR_ = 0.f, pitchRegCoeff_ = .02f;
    float feedback_ = .7f, diffusion_ = .85f, size_ = .8f, sizeScale_ = 1.2f, highCut_ = 11000.f;
    float mix_ = 1.f, modRate_ = .4f, modDepth_ = .1f;
    float damping_ = .35f, apGain_ = .5f, apGainTarget_ = .5f, loopApGain_ = .45f, loopApGainTarget_ = .45f;
    float bloom_ = .5f, bloomS_ = .5f;
    float injectGain_ = .2f, injectTarget_ = .2f, regenGain_ = .2f, regenTarget_ = .2f, freezeAmt_ = 0.f, freezeTarget_ = 0.f, denormSign_ = 1.f;
    uint32_t rng_ = 1u;
    PitchMode pitchMode_ = PitchMode::Dual;
    ReverbMode reverbMode_ = ReverbMode::BigStereo;
    ColorMode color_ = ColorMode::Bright;
    StereoBiquad wetLow_, wetHigh_, pitchLow_, pitchHigh_, aa1_, aa2_;
};
