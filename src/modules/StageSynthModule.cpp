#include "StageSynthModule.h"
#include "../dsp/FastMath.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace SonusDSP {

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

float SingleSynthLayer::readWavetable(int idx, double phase, double step) const {
    if (!wtBank_ || wtBank_->empty()) return 0.f;
    const auto& wt = (*wtBank_)[std::clamp(idx,0,int(wtBank_->size())-1)];
    const double p = (phase - std::floor(phase)) * SYNTH_WAVETABLE_SIZE;
    const int index = int(p) & (SYNTH_WAVETABLE_SIZE-1);
    const int next = (index+1)&(SYNTH_WAVETABLE_SIZE-1);
    const float fraction = float(p - std::floor(p));
    // Prevent harmonics above Nyquist at high pitches, smoothly crossfade mips.
    const float maxHarmonics = 0.45f / std::max(0.00001f,float(step));
    const float lod = std::clamp(std::log2(128.f/std::max(1.f,maxHarmonics)),0.f,7.f);
    const int a=int(lod), b=std::min(7,a+1);
    const float amount=lod-a;
    const auto linear=[&](int mip){return wt.mip[mip][index]*(1.f-fraction)+wt.mip[mip][next]*fraction;};
    return linear(a)*(1.f-amount)+linear(b)*amount;
}

void SingleSynthLayer::init(float sr) {
    runCfg = cfg;
    if (!dx7) dx7 = std::make_shared<Dx7LayerEngine>();
    dx7Buffer.assign(8192, 0.0f);
    // MT-32 runs at the host rate through libmt32emu's converter: rebuilt per rate.
    mt32 = Mt32LayerEngine::create(sr > 8000.0f ? sr : 48000.0f);
    mt32BufferR.assign(8192, 0.0f);
    heldKeys.reserve(128);                 // the audio thread never grows it
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    filter.reset();
    reset();
}

// ---------------------------------------------------------------- LA32 partial
// Roland LA32 synthesized partial (MT-32 / D-50 generation), reimplemented from the
// waveform algorithm documented by the Munt project (LA32FloatWaveGenerator, LGPL):
// the "filter" is not a filter. The square wave is built from two cosine slopes whose
// length is set by the cutoff (16 units per octave above the note, 128 = the note
// itself), resonance adds a decaying sine burst after every edge, and the sawtooth is
// the square multiplied by a cosine at the note frequency. Below cutoff 128 the
// partial is attenuated 12 dB per octave.
namespace {
constexpr float kLa32Pi = 3.14159265358979f;
const float kLa32ResDecay[8] = {31.f, 16.f, 12.f, 8.f, 5.f, 3.f, 2.f, 1.f};

float la32Partial(double phase, float waveLen, float cutoff, float pulseLenFactor,
                  float resAmpBase, float resDecay, bool sawtooth) {
    float cosineLen = 0.5f * waveLen;
    if (cutoff > 128.f) cosineLen *= std::exp2((cutoff - 128.f) / -16.f);
    cosineLen = std::max(cosineLen, 1.0f);
    const float resAmp = (cutoff >= 128.f && cutoff < 144.f)
        ? resAmpBase * std::sin(kLa32Pi * (cutoff - 128.f) / 32.f) : resAmpBase;
    const float wavePos = float(phase) * waveLen;
    float rel = wavePos + 0.5f * cosineLen;
    if (rel > waveLen) rel -= waveLen;
    const float hLen = std::max(0.f, pulseLenFactor * waveLen - cosineLen);
    float square;
    if (rel < cosineLen) square = -std::cos(kLa32Pi * rel / cosineLen);
    else if (rel < cosineLen + hLen) square = 1.f;
    else if (rel < 2.f * cosineLen + hLen) square = std::cos(kLa32Pi * (rel - (cosineLen + hLen)) / cosineLen);
    else square = -1.f;
    float sample = square;
    if (cutoff < 128.f) {
        sample *= std::exp2(-0.125f * (128.f - cutoff));
    } else if (resAmp > 0.f) {
        float resSample = 1.f, decay = resDecay, r = wavePos;
        if (!(r < cosineLen + hLen)) { resSample = -1.f; r -= cosineLen + hLen; decay += 0.25f; }
        resSample *= std::sin(kLa32Pi * r / cosineLen);
        float fade = std::exp2(-0.125f * decay * (r / cosineLen));
        float window = wavePos;
        if (!(wavePos < waveLen - 0.5f * cosineLen)) window -= waveLen;
        else if (!(wavePos < hLen + 0.5f * cosineLen)) window -= cosineLen + hLen;
        if (window < 0.5f * cosineLen) fade *= window < 0.f ? square * square : std::abs(square);
        sample += resSample * resAmp * fade;
    }
    if (sawtooth) sample *= std::cos(2.f * kLa32Pi * float(phase));
    return sample;
}
} // namespace

// DX7 path (synthesisType 5): msfa renders the voices; the slots of `voices` only
// keep the allocation state (key, held / released) shared with the other engines.
static int dx7MidiNote(const SynthLayerConfig& cfg, int playedKey) {
    return std::clamp(playedKey + cfg.octave * 12 + cfg.semitones, 0, 127);
}

void SingleSynthLayer::reset() {
    filter.reset();
    pwmPhase_ = 0.0;
    wheelVibratoPhase_ = 0.0;
    lastNoteFrequency_ = 0.0;
    for (auto& v : voices) {
        v.active = false;
        v.env = 0.0f;
        v.lastOutputL=v.lastOutputR=0.f;
        v.stealCarryL=v.stealCarryR=0.f;
        v.stealFrames=v.stealTotal=0;
        v.filtEnv = 0.0f;
        v.stage = 0;
        v.noiseFilterState = 0.0f;
        v.filterIc1 = v.filterIc2 = v.highpassState = 0.0f;
        v.filterA1 = 1.0f; v.filterA2 = v.filterA3 = 0.0f;
        v.cutoffSmooth = std::clamp(cfg.cutoffHz, 40.0f, 18000.0f);
    }
    for (auto& v : voices) {
        v.fStage = 0;
        v.ladder.fill(0.0f);
        v.syncCarry = 0.0f;
        v.pink.fill(0.0f);
        v.opEnv.fill(0.0f);
        v.opStage.fill(0);
        v.transEnv = 0.0f;
        v.driftRatio = 1.0f;
    }
    lfoRetrigger_ = false;
    lfoElapsed_ = 0.0;
    lfoHeld_ = lfoPrevRandom_ = lfoNextRandom_ = 0.0f;
    smoothedCutoff_ = std::clamp(cfg.cutoffHz, 40.0f, 20000.0f);
    smoothedQ_ = std::clamp(cfg.resonanceQ, 0.5f, 12.0f);
}

namespace {
// Rational tanh approximation (|error| < 2e-3 on ±3), used inside the 4-pole filters.
inline float fastTanh(float x) {
    x = std::clamp(x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// Attack segments aim at 1.2 and stop at 1.0, like an analog contour generator.
// The coefficient makes the segment reach 1.0 in exactly `sec` (ln 6 time constants).
inline float attackCoef(float sec, float sr) {
    return 1.f - std::exp(-1.7917595f / (std::max(.0005f, sec) * sr));
}
// FM4 algorithms. Bit j of mods[i] means operator j modulates operator i (0 = OP1).
// Modulators always have a higher index, so evaluating OP4→OP1 is sufficient.
struct Fm4Algorithm { uint8_t mods[4]; uint8_t carriers; };
constexpr Fm4Algorithm FM4_ALGORITHMS[8] = {
    {{0b0010, 0b0100, 0b1000, 0}, 0b0001},   // 0 STACK      4→3→2→1
    {{0b0010, 0b1100, 0, 0}, 0b0001},        // 1 Y          (3+4)→2→1
    {{0b0110, 0, 0b1000, 0}, 0b0001},        // 2 BRANCH     4→3→1 + 2→1
    {{0b0010, 0, 0b1000, 0}, 0b0101},        // 3 2 STACKS   2→1, 4→3  (FM e-piano)
    {{0b1000, 0b1000, 0b1000, 0}, 0b0111},   // 4 1→3        4→(1,2,3)
    {{0, 0b0100, 0b1000, 0}, 0b0011},        // 5 STACK+1    4→3→2, 1
    {{0, 0, 0b1000, 0}, 0b0111},             // 6 1 MOD      4→3, 1, 2
    {{0, 0, 0, 0}, 0b1111},                  // 7 ADDITIVE
};
constexpr float FM4_MAX_INDEX = 5.0f;   // radians of phase modulation at level 1.0
}

void SingleSynthLayer::releaseAll() {
    if (cfg.synthesisType == 6 && mt32) mt32->allNotesOff();
    heldKeys.clear();
    for (auto& v : voices) {
        if (!v.active) continue;
        v.keyHeld = false;
        v.sustained = false;
        releaseVoice(v);
        v.fastRelease = true;
    }
}

void SingleSynthLayer::releaseVoice(SynthPolyVoice& v) {
    v.stage = 4;
    v.fStage = 4;
    for (auto& s : v.opStage) s = 4;
}

void SingleSynthLayer::noteOn(int rawKey, int playedKey, int velocity) {
    if (cfg.synthesisType == 6) {                    // MT-32: the emulation owns its voices
        if (mt32) {
            mt32->noteOn(std::clamp(playedKey + cfg.octave * 12 + cfg.semitones, 0, 127), velocity);
            voices[0].active = true; voices[0].keyHeld = true; voices[0].rawKey = rawKey; voices[0].playedKey = playedKey;
            heldKeys.push_back({rawKey, playedKey, velocity});
        }
        return;
    }
    if (!cfg.enabled) return;

    const bool noteOverlap = std::any_of(voices.begin(),voices.end(),[](const SynthPolyVoice& v){return v.active&&v.keyHeld;});
    // A new phrase (no keys held) restarts the LFO delay/fade and, if enabled, its cycle.
    if (!noteOverlap) lfoRetrigger_ = true;
    // Si la misma tecla ya estaba activa en esta capa, pasar sus voces previas a liberación rápida
    for (auto& v : voices) {
        if (v.active && v.rawKey == rawKey) {
            v.keyHeld = false;
            v.sustained = false;
            releaseVoice(v);
        }
    }

    float vNorm = std::clamp(velocity, 1, 127) / 127.0f;
    float velSens = std::clamp(cfg.velToAmp, 0.0f, 2.0f);
    float velAmp = (1.0f - 0.65f * velSens * 0.5f) + (vNorm * vNorm) * (0.65f * velSens);
    velAmp = std::clamp(velAmp, 0.15f, 1.5f);

    const int allowed=std::clamp(voiceLimit,1,SYNTH_MAX_VOICES_PER_LAYER);
    int unison = std::clamp(cfg.unisonVoices, 1, std::min(SYNTH_MAX_UNISON,allowed));
    float unisonNorm = 1.0f / std::sqrt(static_cast<float>(unison));

    double keyOffset = (static_cast<double>(playedKey) - 60.0) * static_cast<double>(cfg.keyTrack);
    double pitchNote = 60.0 + keyOffset + cfg.octave * 12.0 + cfg.semitones + (cfg.detuneCents / 100.0);
    double baseFreq = 440.0 * std::pow(2.0, (pitchNote - 69.0) / 12.0);
    const bool transient = cfg.transientLevel > 0.0001f;
    if (voiceMode != 0) {
        bool reused = false;
        for (auto& v : voices) {
            if (!v.active || !v.keyHeld) continue;
            v.rawKey=rawKey;v.playedKey=playedKey;v.targetFreq=baseFreq;
            if (!glideEnabled || (glideMode==1 && !noteOverlap))v.freq=baseFreq;
            v.slidePitch=slideSemis;
            v.velNorm=vNorm;v.velGain=velAmp;
            if (voiceMode == 1) {
                // MONO retriggers every contour from its current level (no discontinuity).
                v.stage=1; v.fStage=1;
                for (auto& s : v.opStage) s = 1;
                if (transient) { v.transEnv = 1.f; v.transPhase = 0.0; }
            }
            reused=true;
        }
        if (reused) {lastNoteFrequency_=baseFreq;return;}
    }
    const double startFreq = (glideEnabled && glideSeconds>0.001f && (glideMode==0 || noteOverlap) && lastNoteFrequency_>0.0)
                                ? lastNoteFrequency_ : baseFreq;
    lastNoteFrequency_=baseFreq;

    for (int u = 0; u < unison; ++u) {
        SynthPolyVoice* target = nullptr;
        for(int i=0;i<allowed;++i){
            auto& v=voices[size_t(i)];
            if (!v.active) { target = &v; break; }
        }
        if (!target) {
            // Robar primero voces en Release dentro del presupuesto permitido.
            float minScore = 1e9f;
            for(int i=0;i<allowed;++i){
                auto& v=voices[size_t(i)];
                float score = v.env + (v.stage == 4 ? 0.0f : 1.0f);
                if (score < minScore) {
                    minScore = score;
                    target = &v;
                }
            }
            if (!target) target = &voices[0];
        }

        // Preserve the last 10 ms of a stolen oscillator's output. Resetting
        // its phases/envelopes without this carry creates an instantaneous
        // waveform step on dense polyphonic passages. No allocation/locks.
        if (target->active) {
            target->stealCarryL = target->lastOutputL;
            target->stealCarryR = target->lastOutputR;
            target->stealTotal = std::max(1,int(sr_ * 0.010f));
            target->stealFrames = target->stealTotal;
        } else {
            target->stealCarryL=target->stealCarryR=0.f;
            target->stealFrames=target->stealTotal=0;
        }
        target->lastOutputL=target->lastOutputR=0.f;
        if (cfg.synthesisType == 5 && dx7)
            dx7->noteOn(int(target - voices.data()), dx7MidiNote(cfg, playedKey), velocity);
        target->modCutMul = target->modAmpMul = target->modPitchMul = 1.f; target->modPanAdd = 0.f;
        target->active = true;
        target->fastRelease = false;
        target->killFade = 1.0f;
        target->rawKey = rawKey;
        target->playedKey = playedKey;
        target->keyHeld = true;
        target->sustained = false;
        target->stage = 1;
        target->fStage = 1;
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
        target->ladder.fill(0.0f);
        target->syncCarry = 0.0f;
        target->pink.fill(0.0f);
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
        for (auto& h : target->hyperPhase) { seed = lcgNext(seed); h = (seed & 0xFFFF) / 65536.0; }

        // Analog character: a fixed per-note offset plus a slow random walk (see renderLayer).
        const float analog = std::clamp(cfg.analog, 0.f, 1.f);
        target->staticCents = analog * 4.f * lcgFloat(seed);
        target->cutoffSpread = 1.f + analog * .08f * lcgFloat(seed);
        target->driftCents = 0.f;
        target->driftTarget = lcgFloat(seed);
        target->driftTimer = 0;
        target->driftRatio = std::exp2(target->staticCents / 1200.f);

        // FM4: operators restart in phase (key sync) with their own contours.
        target->opPhase.fill(0.0);
        target->opEnv.fill(0.f);
        target->opStage.fill(1);
        target->opScale.fill(1.f);
        target->fbPrev1 = target->fbPrev2 = 0.f;

        target->transEnv = transient ? 1.f : 0.f;
        target->transPhase = 0.0;
        target->transBp1 = target->transBp2 = target->transLp = 0.f;
    }
}

void SingleSynthLayer::noteOff(int rawKey, bool pedalDown) {
    if (cfg.synthesisType == 6) {
        if (mt32) {
            for (auto it = heldKeys.begin(); it != heldKeys.end();) {
                if ((*it)[0] == rawKey) { mt32->noteOff(std::clamp((*it)[1] + cfg.octave * 12 + cfg.semitones, 0, 127)); it = heldKeys.erase(it); }
                else ++it;
            }
        }
        (void)pedalDown;                             // the MT-32 receives CC 64 itself
        return;
    }
    for (auto& v : voices) {
        if (v.active && v.rawKey == rawKey && v.keyHeld) {
            v.keyHeld = false;
            if (pedalDown) v.sustained = true;
            else releaseVoice(v);
        }
    }
}

void SingleSynthLayer::setSustain(bool down) {
    if (cfg.synthesisType == 6) { if (mt32) mt32->sustain(down); return; }
    if (!down) {
        for (auto& v : voices) {
            if (v.active && !v.keyHeld && v.sustained) {
                v.sustained = false;
                releaseVoice(v);
            }
        }
    }
}

void SingleSynthLayer::renderLayer(float* mixL, float* mixR, unsigned int numFrames, float sr,
                                   float macroCutoff, const std::vector<Wavetable>& wtBank,
                                   SynthLFO* lfos, const SynthEnvelope* /*envs*/) {
    const SynthLayerConfig& cfg = runCfg;   // v32.3: knob values + global modulation
    if ((!cfg.enabled && !renderWhileDisabled) || cfg.volume <= 0.001f) return;
    if (cfg.synthesisType == 6) {
        // ---- MT-32 (libmt32emu): Part 1 of this layer's own emulated MT-32.
        auto* m = mt32.get();
        if (!m || dx7Buffer.size() < numFrames) {   // no ROMs: silent, notes released
            for (auto& v : voices) v.active = false;
            return;
        }
        m->setProgram(cfg.mt32Program);
        m->setBend(performancePitchBendSemis + cfg.detuneCents * .01f);
        m->setModWheel(std::clamp(performanceModWheel, 0.f, 1.f));
        std::fill(dx7Buffer.begin(), dx7Buffer.begin() + numFrames, 0.f);
        std::fill(mt32BufferR.begin(), mt32BufferR.begin() + numFrames, 0.f);
        m->render(dx7Buffer.data(), mt32BufferR.data(), numFrames);
        const float pan = std::clamp(cfg.pan, -1.f, 1.f);
        const float gain = cfg.volume * 0.9f;
        const float gl = gain * std::min(1.f, 1.f - pan), gr = gain * std::min(1.f, 1.f + pan);
        for (unsigned int n = 0; n < numFrames; ++n) { mixL[n] += dx7Buffer[n] * gl; mixR[n] += mt32BufferR[n] * gr; }
        // One slot stands for the whole MT-32 part while it still sounds.
        voices[0].active = m->active();
        return;
    }
    if (cfg.synthesisType == 5 && dx7) {
        // ---- DX7 (msfa): mono FM output, then layer level, CC1 amp route and pan.
        if (dx7AppliedRevision != cfg.dx7Revision) { dx7->setVoice(cfg.dx7Voice); dx7AppliedRevision = cfg.dx7Revision; }
        const float wheelNow = std::clamp(performanceModWheel, 0.f, 1.f);
        dx7->setControllers(performancePitchBendSemis, wheelNow, cfg.detuneCents);
        for (size_t i = 0; i < voices.size(); ++i) {
            auto& v = voices[i];
            if (!v.active) continue;
            if (v.stage == 4 || (!v.keyHeld && !v.sustained)) dx7->noteOff(int(i));
            if (!dx7->playing(int(i))) { v.active = false; v.stage = 0; }
        }
        if (dx7Buffer.size() < numFrames) return;          // init() reserves 8192 frames
        std::fill(dx7Buffer.begin(), dx7Buffer.begin() + numFrames, 0.f);
        dx7->render(dx7Buffer.data(), numFrames);
        const bool wheelCustom = cfg.wheelMode == 1 || cfg.wheelMode == 2;
        const float gain = cfg.volume * 0.55f * std::pow(10.f,
            wheelNow * std::clamp((wheelCustom ? cfg.wheelAmpDb : 0.f) + performanceWheelGlobalAmpDb, -36.f, 18.f) / 20.f);
        const float angle = (std::clamp(cfg.pan, -1.f, 1.f) + 1.f) * (PI_F * .25f);
        const float gl = fastCos(angle) * 1.41421356f * gain, gr = fastSin(angle) * 1.41421356f * gain;
        for (unsigned int n = 0; n < numFrames; ++n) { mixL[n] += dx7Buffer[n] * gl; mixR[n] += dx7Buffer[n] * gr; }
        return;
    }
    wtBank_ = &wtBank;
    const float killStep = 1.0f / (0.012f * sr);
    const float smooth = std::exp(-16.0f / (0.008f * sr));
    const float atkCoef = attackCoef(cfg.attackSec, sr);
    const float decCoef = std::exp(-5.0f / (std::max(0.005f, cfg.decaySec) * sr));
    const float relCoef = std::exp(-6.0f / (std::max(0.005f, cfg.releaseSec) * sr));
    const float sus = std::clamp(cfg.sustainLevel, 0.0f, 1.0f);
    const float fAtk = attackCoef(cfg.filtAttackSec, sr);
    const float fDec = std::exp(-5.0f / (std::max(0.005f, cfg.filtDecaySec) * sr));
    const float fRel = std::exp(-6.0f / (std::max(0.005f, cfg.filtReleaseSec) * sr));
    const float fSus = std::clamp(cfg.filtSustainLvl, 0.0f, 1.0f);
    const float wheel = std::clamp(performanceModWheel,0.f,1.f);
    // CC1 routing is isolated for each layer. Global cutoff/gain remains an
    // additive performance macro, while the per-layer destinations are optional.
    const bool wheelCustom = cfg.wheelMode == 1 || cfg.wheelMode == 2;
    const bool wheelVibrato = cfg.wheelMode == 0 || cfg.wheelMode == 2;
    const float cutoffShift = std::clamp((wheelCustom?cfg.wheelCutoffOctaves:0.f)+performanceWheelGlobalCutoff,-8.f,8.f);
    const float mainCutoff = std::clamp(cfg.cutoffHz * macroCutoff *
        std::exp2(wheel*cutoffShift), 40.0f, 20000.0f);
    const float resonanceK = 1.0f / std::clamp(cfg.resonanceQ, 0.5f, 12.0f);
    // 4-pole filters: resonance 0..~4.2 (self-oscillation above 4 for the ladder).
    const int filterType = std::clamp(cfg.filterType, 0, SYNTH_FILTER_TYPE_COUNT - 1);
    const bool fourPole = filterType >= 4;
    const bool ladderType = filterType == 4;
    const float resAmount = std::clamp((cfg.resonanceQ - 0.5f) / 9.5f, 0.f, 1.05f);
    const float ladderK = ladderType ? 4.f * resAmount : 3.8f * std::min(1.f, resAmount);
    const float ladderDrive = ladderType ? 1.6f : 0.8f;
    const float ladderComp = 1.f + ladderK * (ladderType ? .5f : .65f);
    const float hpCut = std::clamp(cfg.highpassHz, 20.0f, sr * 0.40f);
    const float hpAlpha = std::exp(-2.0f * PI_F * hpCut / sr);
    const float osc2Ratio = std::pow(2.0f, (cfg.osc2Semitones + cfg.osc2Detune / 100.0f) / 12.0f);
    const float layerGain = cfg.volume * 0.20f * std::pow(10.f,
        wheel*std::clamp((wheelCustom?cfg.wheelAmpDb:0.f)+performanceWheelGlobalAmpDb,-36.f,18.f)/20.f);
    const float basePW = std::clamp(cfg.pulseWidth, 0.05f, 0.95f);
    const float pwmDepth = std::clamp(cfg.pwmDepth, 0.0f, 0.45f);
    const float sawLevel = std::clamp(cfg.sawLevel, 0.0f, 1.0f);
    const float pulseLevel = std::clamp(cfg.pulseLevel, 0.0f, 1.0f);
    const float subLevel = std::clamp(cfg.subLevel, 0.0f, 1.0f);
    const float noiseLevel = std::clamp(cfg.noiseLevel, 0.0f, 1.0f);
    const float osc2Level = std::clamp(cfg.osc2Level, 0.0f, 1.0f);
    // LA32 resonance (0-30 on the MT-32) from the filter RESO knob.
    const int la32Res = std::clamp(int(std::lround((std::clamp(cfg.resonanceQ, 0.5f, 12.f) - 0.5f) / 11.5f * 30.f)), 0, 30);
    const float la32ResBase = la32Res > 0 ? std::exp2(1.f - float(32 - la32Res) / 4.f) : 0.f;
    const float la32ResDecay = kLa32ResDecay[la32Res >> 2];
    const float analogNorm = 1.0f / std::max(1.0f, sawLevel + pulseLevel + subLevel + noiseLevel + osc2Level);
    const float driveAmount = std::clamp(cfg.drive, 0.0f, 1.0f);
    // v32.4: drive normalisation once per block (was two std::tanh per sample).
    const float driveGain = 1.0f + driveAmount * 3.0f, driveNorm = 1.0f / fastTanh(driveGain);
    const float analog = std::clamp(cfg.analog, 0.f, 1.f);
    const int synthesisType = std::clamp(cfg.synthesisType, 0, SYNTH_SYNTHESIS_TYPE_COUNT - 1);
    // Hypersaw: six side saws spread symmetrically (5..50 cents) around the centre saw.
    std::array<double,6> hyperRatio{};
    {
        static constexpr float offsets[6] = {-1.f, -.62f, -.27f, .27f, .62f, 1.f};
        const float spread = 5.f + 45.f * osc2Level;
        for (int k = 0; k < 6; ++k) hyperRatio[size_t(k)] = std::exp2(double(offsets[k] * spread) / 1200.0);
    }
    // FM4 operator contours and routing.
    const Fm4Algorithm& algorithm = FM4_ALGORITHMS[std::clamp(cfg.fm4Algorithm, 0, 7)];
    std::array<float,4> opAtk{}, opDec{}, opSus{}, opLvl{}, opRat{};
    int carrierCount = 0;
    float maxCarrierRatio = 0.f;
    for (int i = 0; i < 4; ++i) {
        const size_t k = size_t(i);
        opAtk[k] = attackCoef(cfg.opAttack[k], sr);
        opDec[k] = std::exp(-5.0f / (std::max(0.005f, cfg.opDecay[k]) * sr));
        opSus[k] = std::clamp(cfg.opSustain[k], 0.f, 1.f);
        opLvl[k] = std::clamp(cfg.opLevel[k], 0.f, 1.f);
        opRat[k] = std::clamp(cfg.opRatio[k], .5f, 16.f);
        if (algorithm.carriers & (1u << i)) { ++carrierCount; maxCarrierRatio = std::max(maxCarrierRatio, opRat[k]); }
    }
    const float carrierNorm = .8f / std::sqrt(float(std::max(1, carrierCount)));
    const float fm4Feedback = std::clamp(cfg.fm4Feedback, 0.f, 1.f) * 2.5f;
    const float fm4VelMod = std::clamp(cfg.fm4VelMod, 0.f, 1.f);
    // LA hybrid and attack transient.
    const float laMix = std::clamp(cfg.laMix, 0.f, 1.f);
    const float laRing = std::clamp(cfg.laRing, 0.f, 1.f);
    const int transientType = std::clamp(cfg.transientType, 0, SYNTH_TRANSIENT_COUNT - 1);
    const float transientLevel = std::clamp(cfg.transientLevel, 0.f, 1.f);
    const float transientTone = std::clamp(cfg.transientTone, 0.f, 1.f);
    // Fixed-length attacks: CLICK is always short, BOW swells and fades.
    const float transientSec = transientType == 4 ? std::min(.012f, std::max(.002f, cfg.transientDecay))
                                                  : std::clamp(cfg.transientDecay, .005f, 4.f);
    const float transDecayCoef = std::exp(-5.f / (transientSec * sr));
    const float noiseBandCoef = std::min(.95f, 2.f * PI_F * (500.f + 5500.f * transientTone) / sr);
    const float noiseLowCoef = 1.f - std::exp(-2.f * PI_F * (800.f + 9000.f * transientTone) / sr);

    // Each layer owns a free-running LFO. Advance it at audio rate rather than
    // once per callback; a 1024+ frame ASIO buffer must not alias a 20-30 Hz LFO.
    SynthLFO* lfo=lfos;
    const bool lfoOn=lfo && lfo->enabled;
    double lfoPhase=lfo?lfo->phaseAccum:0.0;
    if (lfoRetrigger_) {
        lfoRetrigger_ = false;
        lfoElapsed_ = 0.0;
        if (lfo && lfo->keySync) lfoPhase = 0.0;
    }
    const double lfoStep=lfoOn ? std::clamp(double(lfo->rateHz),.05,30.0)/sr : 0.0;
    const float lfoDepth=lfoOn?std::clamp(lfo->depth,0.f,1.f):0.f;
    const int lfoWave = lfoOn ? std::clamp(lfo->waveType, 0, SYNTH_LFO_WAVE_COUNT - 1) : 0;
    const double lfoDelay = lfoOn ? std::clamp(double(lfo->delaySec), 0.0, 10.0) * sr : 0.0;
    const double lfoFade = lfoOn ? std::clamp(double(lfo->fadeSec), 0.0, 10.0) * sr : 0.0;
    float lastLfoValue=0.f;

    const float maxCutoff = std::min(20000.0f, sr * 0.44f);
    const float bendRatio = std::pow(2.f, performancePitchBendSemis / 12.f);
    // A sequencer SLIDE step glides even when the layer's portamento is off.
    const float effGlide = seqGlideSeconds > 0.f ? seqGlideSeconds : (glideEnabled ? glideSeconds : 0.f);
    const float glideAlpha = effGlide < 0.001f ? 1.0f : 1.0f-std::exp(-1.0f/(std::max(.001f,effGlide)*sr));
    const float slideDecay = std::exp(-1.0f/(.15f*sr));
    for (unsigned int n = 0; n < numFrames; ++n) {
        float lfoValue=0.f;
        if(lfoOn){
            lfoPhase+=lfoStep;
            bool wrapped=false;
            if(lfoPhase>=1.0){lfoPhase -= std::floor(lfoPhase);wrapped=true;}
            const float p=static_cast<float>(lfoPhase);
            switch(lfoWave){
                case 1: lfoValue=1.f-4.f*std::abs(p-.5f); break;
                case 2: lfoValue=2.f*p-1.f; break;
                case 3: lfoValue=p<.5f?1.f:-1.f; break;
                case 4: // sample & hold: a new random value every cycle
                    if(wrapped)lfoHeld_=lcgFloat(lfoSeed_);
                    lfoValue=lfoHeld_; break;
                case 5: // smooth random: cosine glide between random targets
                    if(wrapped){lfoPrevRandom_=lfoNextRandom_;lfoNextRandom_=lcgFloat(lfoSeed_);}
                    lfoValue=lfoPrevRandom_+(lfoNextRandom_-lfoPrevRandom_)*(.5f-.5f*fastCos(PI_F*p)); break;
                default: lfoValue=fastSin(2.f*PI_F*p); break;
            }
            float lfoGain=1.f;
            if(lfoDelay>0.0||lfoFade>0.0){
                if(lfoElapsed_<lfoDelay)lfoGain=0.f;
                else if(lfoFade>0.0)lfoGain=float(std::min(1.0,(lfoElapsed_-lfoDelay)/lfoFade));
            }
            lfoValue*=lfoDepth*lfoGain;
        }
        lfoElapsed_+=1.0;
        lastLfoValue=lfoValue;
        const float pitchLfo=lfoOn?std::exp2(lfoValue*float(lfo->destPitch)/1200.f):1.f;
        const float filterLfo=lfoOn?lfoValue*float(lfo->destCutoff):0.f;
        const float ampLfo=lfoOn?std::clamp(1.f+lfoValue*lfo->destVolume,0.f,2.f):1.f;
        const float panLfo=lfoOn?lfoValue*lfo->destPan:0.f;
        // Wheel vibrato runs on its own phase per synth layer, independently
        // of both the PWM oscillator and the layer's assignable LFO.
        wheelVibratoPhase_ += std::clamp(double(cfg.wheelVibratoRateHz),0.1,12.0)/sr;
        if(wheelVibratoPhase_>=1.0)wheelVibratoPhase_ -= std::floor(wheelVibratoPhase_);
        const float vibCents=(wheelVibrato?wheel:0.f)*std::clamp(cfg.wheelVibratoCents,0.f,200.f)*
            fastSin(2.f*PI_F*static_cast<float>(wheelVibratoPhase_));
        const bool controlTick=(n&15u)==0u;
        if(controlTick){
            for(auto& v:voices)if(v.active){
                if(modSnap && modSnap->count>0){
                    // Voice targets: cutoff (octaves), level, pan, pitch (semitones).
                    float cutOct=0.f, level=0.f, panAdd=0.f, semis=0.f;
                    for(int k=0;k<modSnap->count;++k){
                        const auto& c=modSnap->list[size_t(k)];
                        if(c.layer!=index || c.voice==MVT_NONE)continue;
                        float s;
                        switch(c.source){
                            case MOD_VELOCITY: s=v.velNorm; break;
                            case MOD_KEY: s=std::clamp((v.playedKey-60)/60.f,-1.f,1.f); break;
                            case MOD_AMP_ENV: s=std::min(v.env,1.f); break;
                            case MOD_FILTER_ENV: s=v.filtEnv; break;
                            default: s=modGlobal[size_t(c.source)]; break;
                        }
                        const float m=c.amount*modShape(s,modSourceIsBipolar(c.source),c.bipolar,c.curve);
                        if(c.voice==MVT_CUTOFF)cutOct+=m*8.966f;            // log2(20000/40)
                        else if(c.voice==MVT_LEVEL)level+=m;
                        else if(c.voice==MVT_PAN)panAdd+=m*2.f;
                        else if(c.voice==MVT_PITCH)semis+=m*24.f;
                    }
                    v.modCutMul=std::exp2(std::clamp(cutOct,-10.f,10.f));
                    v.modPanAdd=panAdd;
                    v.modPitchMul=std::exp2(std::clamp(semis,-48.f,48.f)/12.f);
                    const float base=cfg.volume/1.5f, mod=std::clamp(base+level,0.f,1.f);
                    v.modAmpMul=cfg.volume>1e-3f?std::min(8.f,mod*1.5f/cfg.volume):0.f;
                }else{ v.modCutMul=1.f; v.modAmpMul=1.f; v.modPanAdd=0.f; v.modPitchMul=1.f; }
                const float pan=std::clamp(cfg.pan+v.panOffset+panLfo+v.modPanAdd,-1.f,1.f);
                const float angle=(pan+1.f)*(PI_F*.25f);
                v.panGainL=fastCos(angle);v.panGainR=fastSin(angle);
            }
        }
        pwmPhase_ += std::clamp(cfg.pwmRateHz, 0.05f, 12.0f) / sr;
        if (pwmPhase_ >= 1.0) pwmPhase_ -= 1.0;
        const float pw = std::clamp(basePW + pwmDepth *
            fastSin(2.0f * PI_F * static_cast<float>(pwmPhase_)), 0.05f, 0.95f);
        float sumL = 0.0f, sumR = 0.0f;

        for (auto& v : voices) {
            if (!v.active) continue;
            switch (v.stage) {
                case 1:
                    v.env += (1.2f - v.env) * atkCoef;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; }
                    break;
                case 2:
                    v.env = sus + (v.env - sus) * decCoef;
                    if (std::abs(v.env - sus) < 1e-4f) { v.env = sus; v.stage = 3; }
                    break;
                case 3:
                    v.env = sus;
                    break;
                case 4:
                    v.env *= relCoef;
                    if (v.env < 1e-4f) { v.env = 0.0f; v.active = false; }
                    break;
                default: break;
            }
            if (!v.active) continue;
            // The filter contour runs its own stages: a long filter attack is never
            // cut short by a fast amplitude attack (and vice versa).
            switch (v.fStage) {
                case 1:
                    v.filtEnv += (1.2f - v.filtEnv) * fAtk;
                    if (v.filtEnv >= 1.0f) { v.filtEnv = 1.0f; v.fStage = 2; }
                    break;
                case 2: v.filtEnv = fSus + (v.filtEnv - fSus) * fDec; break;
                case 4: v.filtEnv *= fRel; break;
                default: break;
            }
            if (synthesisType == 3) {
                for (size_t i = 0; i < 4; ++i) {
                    switch (v.opStage[i]) {
                        case 1:
                            v.opEnv[i] += (1.2f - v.opEnv[i]) * opAtk[i];
                            if (v.opEnv[i] >= 1.f) { v.opEnv[i] = 1.f; v.opStage[i] = 2; }
                            break;
                        case 2: v.opEnv[i] = opSus[i] + (v.opEnv[i] - opSus[i]) * opDec[i]; break;
                        case 4: v.opEnv[i] *= relCoef; break;
                        default: break;
                    }
                }
            }

            if (controlTick && analog > 0.f) {
                // Slow random walk: a new target every ~0.3-0.6 s, followed smoothly.
                if (--v.driftTimer <= 0) {
                    v.driftTarget = lcgFloat(v.noiseSeed);
                    v.driftTimer = int(sr / 16.f * (.3f + .3f * std::abs(v.driftTarget)));
                }
                v.driftCents += (v.driftTarget - v.driftCents) * .02f;
                v.driftRatio = std::exp2((v.staticCents + analog * 5.f * v.driftCents) / 1200.f);
            }

            v.freq += (v.targetFreq - v.freq) * glideAlpha;
            v.slidePitch *= slideDecay;
            // v32.4: exp2 only while vibrato / slide move the pitch (it ran per sample, per voice).
            const float perfSemis = vibCents / 100.f + v.slidePitch;
            const double performanceRatio = bendRatio * (std::abs(perfSemis) > 1e-6f ? std::exp2(perfSemis / 12.f) : 1.f) * v.driftRatio;
            const double inc = std::clamp(v.freq * v.detuneMult * pitchLfo * performanceRatio * v.modPitchMul / sr,
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
            auto tri = [](double phase) -> float { return static_cast<float>(1.0 - 4.0 * std::abs(phase - 0.5)); };
            // Band-limited hard sync: phase2 (slave saw) restarts when phase1 (master) wraps.
            // Both the slave's own wrap and the reset step get a two-sample polyBLEP residual:
            // the pre-step part is added now, the post-step part on the next sample.
            auto syncSaw = [&](double slaveInc) -> float {
                slaveInc = std::clamp(slaveInc, 1e-7, .45);
                double ps = v.phase2;
                float out = float(2.0 * ps - 1.0) + v.syncCarry;
                float carry = 0.f;
                const bool reset = v.phase1 + inc >= 1.0;
                const double f = reset ? (1.0 - v.phase1) / inc : 1.0;
                if (ps + slaveInc * f >= 1.0) {
                    const double fn = (1.0 - ps) / slaveInc;
                    out -= float((1.0 - fn) * (1.0 - fn));        // h = -2
                    carry += float(fn * fn);
                    ps -= 1.0;
                }
                if (reset) {
                    const double at = std::clamp(ps + slaveInc * f, 0.0, 1.0);
                    const float h = float(-2.0 * at);
                    out += h * .5f * float((1.0 - f) * (1.0 - f));
                    carry -= h * .5f * float(f * f);
                    ps = (1.0 - f) * slaveInc;
                } else {
                    ps += slaveInc;
                }
                v.phase2 = ps - std::floor(ps);
                v.syncCarry = carry;
                return out;
            };
            float osc = 0.0f;
            if(synthesisType == 0) switch (cfg.waveType) {
                case 0: osc = saw(v.phase1, inc); break;
                case 1: osc = pulse(v.phase1, inc, pw) * 0.85f; break;
                case 2: osc = tri(v.phase1); break;
                case 3: osc = fastSin(2.0f * PI_F * static_cast<float>(v.phase1)); break;
                case 4: {
                    const float s1 = saw(v.phase1, inc);
                    const float s2 = saw(v.phase2, std::min(0.45, inc * 1.0065));
                    const float s3 = saw(v.phase3, inc * 0.9935);
                    osc = (s1 + 0.82f * (s2 + s3)) * 0.44f;
                    v.phase2 += inc * 1.0065;
                    v.phase3 += inc * 0.9935;
                    if (v.phase2 >= 1.0) v.phase2 -= std::floor(v.phase2);
                    if (v.phase3 >= 1.0) v.phase3 -= std::floor(v.phase3);
                    break;
                }
                case 5:
                    osc = lcgFloat(v.noiseSeed) * 0.55f;
                    break;
                case 6:
                    osc = readWavetable(cfg.wavetableIdx, v.phase1, inc);
                    break;
                case 7: // hard sync (slave ratio follows OSC 2 tuning)
                    osc = syncSaw(inc * (2.0 + 0.5 * osc2Ratio));
                    break;
                case 10: { // two-operator FM with controlled index
                    const float carrier = static_cast<float>(2.0 * PI_F * v.phase1);
                    const float modulator = fastSin(2.0f*PI_F*static_cast<float>(v.phase2));
                    osc = fastSin(carrier + (1.0f + osc2Level*4.0f)*modulator) * .78f;
                    v.phase2 += std::min(.45, inc * std::max(1.0f,osc2Ratio*2.0f));
                    if (v.phase2 >= 1.0) v.phase2 -= std::floor(v.phase2);
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
                    osc = saw(v.phase1,inc) * fastSin(2.0f*PI_F*static_cast<float>(v.phase2));
                    v.phase2=std::fmod(v.phase2+std::min(.45,inc*osc2Ratio*1.5),1.0);
                    break;
                }
                case 13: { // vocal-formant additive oscillator, bounded harmonics
                    const float p=2.0f*PI_F*static_cast<float>(v.phase1);
                    osc = (.35f*fastSin(p)+.45f*fastSin(2*p)+.26f*fastSin(3*p)
                           +.15f*fastSin(5*p)+.10f*fastSin(7*p))*.72f;
                    break;
                }
                case 14: { // wavefolder with smooth nonlinear folding
                    const float x=1.0f+2.0f*osc2Level;
                    osc = fastSin(x*2.7f*fastSin(2.0f*PI_F*static_cast<float>(v.phase1)))*.75f;
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
                    if (cfg.osc2Wave == 2) oscB = tri(v.phase2);
                    osc = analogNorm * (sawLevel * saw(v.phase1, inc) +
                        pulseLevel * pulse(v.phase1, inc, pw) +
                        subLevel * pulse(v.phaseSub, inc * 0.5, 0.50f) +
                        noiseLevel * lcgFloat(v.noiseSeed) + osc2Level * oscB);
                    v.phase2 += inc2;
                    if (v.phase2 >= 1.0) v.phase2 -= std::floor(v.phase2);
                    break;
                }
                case 15: osc = .62f*fastSin(2.f*PI_F*float(v.phase1)) + .25f*fastSin(6.f*PI_F*float(v.phase1));break; // harmonic sine
                case 16: osc = .58f*saw(v.phase1,inc) + .30f*pulse(v.phase1,inc,.28f);break; // razor
                case 17: osc = fastSin(2.f*PI_F*float(v.phase1) + 1.5f*fastSin(4.f*PI_F*float(v.phase1)));break; // phase warp
                case 18: osc = .72f*fastSin(2.f*PI_F*float(v.phase1))+.22f*fastSin(8.f*PI_F*float(v.phase1));break; // organ harmonics
                case 19: osc = .55f*saw(v.phase1,inc)+.27f*fastSin(10.f*PI_F*float(v.phase1));break; // spectral saw
                case 20: osc = std::tanh(2.2f*saw(v.phase1,inc))*.66f;break; // saturated saw
                case 21: { // PWM ensemble: BLEP discontinuities, three detuned pulses
                    osc=(pulse(v.phase1,inc,pw)+.76f*pulse(v.phase2,std::min(.45,inc*1.004),pw)+
                         .76f*pulse(v.phase3,std::min(.45,inc*.996),pw))*.40f;
                    v.phase2=std::fmod(v.phase2+inc*1.004,1.);
                    v.phase3=std::fmod(v.phase3+inc*.996,1.);
                    break;
                }
                case 22: { // inharmonic bell, inharmonicity softened at high MIDI keys
                    const float p=2.f*PI_F*float(v.phase1);
                    osc=.66f*fastSin(p)+.22f*fastSin(p*2.405f)+.11f*fastSin(p*3.72f);
                    break;
                }
                case 23: { // octave/fifth organ with band limiting at high notes
                    const float p=2.f*PI_F*float(v.phase1);
                    osc=.60f*fastSin(p)+.28f*fastSin(2.f*p)+
                        (inc<.28?.15f*fastSin(3.f*p):0.f);
                    break;
                }
                case 24: { // Reese bass: twin detuned BLEP saws and sub
                    osc=.44f*saw(v.phase1,inc)+.37f*saw(v.phase2,std::min(.45,inc*1.013))+
                        .20f*pulse(v.phaseSub,inc*.5f,.5f);
                    v.phase2=std::fmod(v.phase2+inc*1.013,1.);
                    break;
                }
                case 25: { // softly modulated phase/FM glass
                    const float mod=fastSin(2.f*PI_F*float(v.phase2));
                    const float maxIndex=std::clamp((.44f/float(inc)-1.f)/2.5f-2.f,0.f,3.5f);
                    osc=.75f*fastSin(2.f*PI_F*float(v.phase1)+std::min(2.5f,maxIndex)*mod);
                    v.phase2=std::fmod(v.phase2+std::min(.44,inc*2.5),1.);
                    break;
                }
                case 26: // vocal spectral wavetable: upper harmonics reduced per mip
                    osc=readWavetable(10,v.phase1,inc)*.62f+
                        readWavetable(9,v.phase1,inc)*.38f;
                    break;
                case 27: // bright raspy sync
                    osc=syncSaw(inc*3.5)*.85f;
                    break;
                case 28: // Mip-mapped triangle ensemble
                    osc=.65f*readWavetable(1,v.phase1,inc)+
                        .24f*readWavetable(1,v.phase2,std::min(.44,inc*1.006));
                    v.phase2=std::fmod(v.phase2+std::min(.44,inc*1.006),1.);
                    break;
                case 29: case 30: { // Casio-style phase distortion; OSC2 level = DCW amount,
                    // shaped by the filter envelope like a CZ "DCW" contour.
                    const float dcw = std::clamp(osc2Level * (.25f + .75f * v.filtEnv), 0.f, 1.f);
                    const float p = float(v.phase1);
                    if (cfg.waveType == 29) {
                        // Bend the phase so a cosine becomes a saw; corner softened at high notes.
                        const float amount = dcw * std::clamp(1.f - float(inc) * 6.f, 0.f, 1.f);
                        const float d = .5f - .47f * amount;
                        const float w = p < d ? p * (.5f / d) : .5f + (p - d) * (.5f / (1.f - d));
                        osc = -fastCos(2.f * PI_F * w) * .8f;
                    } else {
                        // Resonant PD: windowed sine at 1-8× the note, continuous at the wrap.
                        const float r = std::min(1.f + 7.f * dcw, float(.4 / std::max(inc, 1e-6)));
                        osc = ((1.f - p) * (1.f - fastCos(2.f * PI_F * r * p)) - .5f) * .9f;
                    }
                    break;
                }
                case 31: { // Triple VCO (1970s mono): VCO1 saw, VCO2 tuned by OSC2, VCO3 an octave down
                    float o2 = saw(v.phase2, inc2);
                    if (cfg.osc2Wave == 1) o2 = pulse(v.phase2, inc2, pw);
                    else if (cfg.osc2Wave == 2) o2 = tri(v.phase2);
                    const double inc3 = std::min(.45, inc * .5 * 1.0009);
                    const float o3 = saw(v.phase3, inc3);
                    const float norm = 1.f / std::max(1.f, sawLevel + osc2Level + subLevel + noiseLevel);
                    osc = norm * (sawLevel * saw(v.phase1, inc) + osc2Level * o2 + subLevel * o3 +
                                  noiseLevel * lcgFloat(v.noiseSeed));
                    v.phase2 += inc2; if (v.phase2 >= 1.0) v.phase2 -= std::floor(v.phase2);
                    v.phase3 += inc3; if (v.phase3 >= 1.0) v.phase3 -= std::floor(v.phase3);
                    break;
                }
                case 32: { // Sync sweep: the filter envelope sweeps the slave up to 3 octaves (poly-mod)
                    const double ratio = std::clamp(double(osc2Ratio) * std::exp2(double(osc2Level) * 3.0 * v.filtEnv), 1.0, 16.0);
                    osc = syncSaw(inc * ratio) * .85f;
                    break;
                }
                case 33: { // Hypersaw: seven band-limited saws
                    float sum = saw(v.phase1, inc);
                    for (size_t k = 0; k < 6; ++k) {
                        const double ik = std::min(.45, inc * hyperRatio[k]);
                        sum += .72f * saw(v.hyperPhase[k], ik);
                        v.hyperPhase[k] += ik; if (v.hyperPhase[k] >= 1.0) v.hyperPhase[k] -= std::floor(v.hyperPhase[k]);
                    }
                    osc = sum * .30f;
                    break;
                }
                case 34: { // Pink noise (Kellet economy filter)
                    const float w = lcgFloat(v.noiseSeed);
                    v.pink[0] = .99765f * v.pink[0] + w * .0990460f;
                    v.pink[1] = .96300f * v.pink[1] + w * .2965164f;
                    v.pink[2] = .57000f * v.pink[2] + w * 1.0526913f;
                    osc = (v.pink[0] + v.pink[1] + v.pink[2] + w * .1848f) * .16f;
                    break;
                }
                case 35: { // 8-bit pulse: duty quantised to 12.5 / 25 / 50 / 75 %
                    static constexpr float duties[4] = {.125f, .25f, .5f, .75f};
                    osc = pulse(v.phase1, inc, duties[std::clamp(int(basePW * 4.f), 0, 3)]) * .7f;
                    break;
                }
                default: osc = fastSin(2.0f * PI_F * static_cast<float>(v.phase1)); break;
            }
            if (synthesisType == 1) {
                const double modPhase=v.phase2;
                const float mod=fastSin(2.f*PI_F*float(modPhase));
                // Phase-modulation sidebands can exceed Nyquist even when the
                // carrier itself is within range. Dynamically taper index at
                // high notes rather than letting every bright FM patch alias.
                const float ratio=std::clamp(cfg.fmRatio,.25f,12.f);
                const float sidebandBudget=std::max(0.f, (.45f/std::max(float(inc),1.e-5f)-1.f)/ratio-1.5f);
                const float index=std::min(std::clamp(cfg.fmIndex,0.f,8.f),sidebandBudget);
                osc=fastSin(2.f*PI_F*float(v.phase1)+index*mod)*.8f;
                v.phase2=std::fmod(v.phase2+std::min(.45,inc*std::clamp(double(cfg.fmRatio),.25,12.)),1.);
            } else if ((synthesisType == 2 || synthesisType == 4) && !wtBank.empty()) {
                const int index=std::clamp(cfg.wavetableIdx,0,int(wtBank.size())-1);
                const int next=(index+1)%int(wtBank.size());
                const float morph=std::clamp(cfg.wavetableMorph+(wheelCustom?wheel:0.f)*cfg.wheelWtMorph,0.f,1.f);
                const float table=readWavetable(index,v.phase1,inc)*(1.f-morph)+readWavetable(next,v.phase1,inc)*morph;
                if (synthesisType == 2) osc = table;
                else {
                    // LA hybrid: wavetable partial + DCO partial (saw/pulse mixer), optional ring.
                    const float dcoSum = sawLevel + pulseLevel;
                    const float dco = dcoSum > .001f
                        ? (sawLevel * saw(v.phase1, inc) + pulseLevel * pulse(v.phase1, inc, pw)) / std::max(1.f, dcoSum)
                        : 0.f;
                    osc = 1.6f * ((1.f - laRing) * ((1.f - laMix) * table + laMix * dco) + laRing * table * dco * 1.5f);
                }
            } else if (synthesisType == 7) {
                // LA32: partial 1 = mixer (SAW or PULSE, PW), partial 2 = OSC 2 (ratio,
                // level, wave), RING = ring modulation. The TVF cutoff (filter section and
                // its envelope, per voice) drives the LA32 waveform; the filter is bypassed.
                const float hz = float(inc) * sr;
                const float waveLen = 1.f / std::max(float(inc), 1e-6f);
                const float cutoffLa = std::clamp(128.f + 16.f * std::log2(std::max(v.cutoffSmooth, 1.f) / std::max(hz, 1.f)), 0.f, 255.f);
                const float pulseFactor = std::clamp(pw <= .5f ? pw : 1.f - pw, .126f, .5f);
                const bool saw1 = sawLevel >= pulseLevel;
                const float p1 = la32Partial(v.phase1, waveLen, cutoffLa, pulseFactor, la32ResBase, la32ResDecay, saw1);
                float mixed = p1 * std::max(sawLevel, pulseLevel);
                if (osc2Level > 0.0001f) {
                    const double inc2 = std::min(.45, inc * double(osc2Ratio));
                    const float waveLen2 = 1.f / std::max(float(inc2), 1e-6f);
                    const float cutoff2 = std::clamp(cutoffLa - 16.f * std::log2(std::max(osc2Ratio, .01f)), 0.f, 255.f);
                    const float p2 = la32Partial(v.phase2, waveLen2, cutoff2, pulseFactor, la32ResBase, la32ResDecay, cfg.osc2Wave != 1);
                    const float ring = std::clamp(cfg.laRing, 0.f, 1.f);
                    mixed += osc2Level * ((1.f - ring) * p2 + ring * p1 * p2);
                    v.phase2 += inc2;
                    if (v.phase2 >= 1.0) v.phase2 -= std::floor(v.phase2);
                }
                osc = mixed * .5f;
            } else if (synthesisType == 3) {
                // FM4: four sine operators. Modulator outputs are phase offsets in radians,
                // limited per note so that the strongest sidebands stay below Nyquist.
                const float velDepth = 1.f - fm4VelMod * (1.f - v.velNorm);
                std::array<float,4> out{};
                float carriers = 0.f;
                for (int i = 3; i >= 0; --i) {
                    const size_t k = size_t(i);
                    float m = 0.f;
                    for (int j = 0; j < 4; ++j) if (algorithm.mods[k] & (1u << j)) m += out[size_t(j)];
                    if (i == 3 && fm4Feedback > 0.f) m += fm4Feedback * .5f * (v.fbPrev1 + v.fbPrev2);
                    const float s = fastSin(2.f * PI_F * float(v.opPhase[k]) + m);
                    if (algorithm.carriers & (1u << i)) {
                        carriers += s * opLvl[k] * v.opEnv[k];
                    } else {
                        out[k] = s * opLvl[k] * v.opEnv[k] * FM4_MAX_INDEX * velDepth * v.opScale[k];
                    }
                    if (i == 3) { v.fbPrev2 = v.fbPrev1; v.fbPrev1 = s; }
                    v.opPhase[k] += std::min(.45, inc * double(opRat[k]));
                    if (v.opPhase[k] >= 1.0) v.opPhase[k] -= std::floor(v.opPhase[k]);
                }
                osc = carriers * carrierNorm;
            }
            v.phase1 += inc;
            if (v.phase1 >= 1.0) v.phase1 -= std::floor(v.phase1);
            v.phaseSub += inc * 0.5;
            if (v.phaseSub >= 1.0) v.phaseSub -= std::floor(v.phaseSub);

            // Per-note filter: recalculate expensive frequency coefficients at 1/16th
            // audio rate; run the actual filter at full sample rate.
            if (controlTick) {
                const float keyRatio = std::exp2((v.playedKey - 60) *
                    (cfg.filterKeyTrack / 12.0f));
                const float envRatio = std::exp2(std::clamp(cfg.filterEnvAmt,
                    -1.0f, 1.0f) * 5.0f * v.filtEnv);
                const float velRatio = 1.0f + cfg.velToCutoff * v.velNorm * 2.0f;
                const float cutoff = std::clamp((mainCutoff + filterLfo) *
                    envRatio * velRatio * keyRatio * v.cutoffSpread * v.modCutMul, 30.0f, maxCutoff);
                v.cutoffSmooth = std::clamp(cutoff + smooth * (v.cutoffSmooth - cutoff), 30.0f, maxCutoff);
                const float g = std::tan(PI_F * v.cutoffSmooth / sr);
                v.filterA1 = 1.0f / (1.0f + g * (g + resonanceK));
                v.filterA2 = g * v.filterA1;
                v.filterA3 = g * v.filterA2;
                v.ladderG = g / (1.f + g);
                if (fourPole) for (auto& s : v.ladder) if (std::abs(s) < 1e-20f) s = 0.f;
                if (synthesisType == 3) {
                    // Sideband budget per modulator: (index + 1) · ratio · f0 must stay < 0.45 · sr.
                    const float room = .45f / std::max(float(inc), 1e-6f) - maxCarrierRatio;
                    for (size_t i = 0; i < 4; ++i) {
                        const float maxIndex = std::max(0.f, room / opRat[i] - 1.f);
                        const float wanted = opLvl[i] * FM4_MAX_INDEX + 1e-6f;
                        v.opScale[i] = std::clamp(maxIndex / wanted, 0.f, 1.f);
                    }
                }
            }

            // One-pole high-pass (pre-VCF) + filter.
            v.highpassState = (1.0f - hpAlpha) * osc + hpAlpha * v.highpassState;
            float input = osc - v.highpassState;
            if (driveAmount > 0.001f) input = fastTanh(input * driveGain) * driveNorm;
            float filtered;
            if (synthesisType == 7) {
                filtered = input;                    // LA32: the waveform is already "filtered"
            } else if (fourPole) {
                // 4-pole low-pass: four TPT one-pole stages with a solved (zero-delay)
                // feedback path and a saturating input stage. 24 dB/oct.
                const float G = v.ladderG, b = 1.f - G;
                auto& s = v.ladder;
                const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
                const float sigma = b * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
                const float y4 = (G4 * input + sigma) / (1.f + ladderK * G4);
                float x = fastTanh((input - ladderK * y4) * ladderDrive) / ladderDrive;
                for (size_t i = 0; i < 4; ++i) {
                    const float vv = (x - s[i]) * G;
                    const float y = vv + s[i];
                    s[i] = y + vv;
                    x = y;
                }
                filtered = x * ladderComp;
            } else {
                const float a1 = v.filterA1;
                const float a2 = v.filterA2;
                const float a3 = v.filterA3;
                const float v3 = input - v.filterIc2;
                const float bp = a1 * v.filterIc1 + a2 * v3;
                const float lp = v.filterIc2 + a2 * v.filterIc1 + a3 * v3;
                v.filterIc1 = 2.0f * bp - v.filterIc1;
                v.filterIc2 = 2.0f * lp - v.filterIc2;
                filtered = lp;
                if (filterType == 1) filtered = input - resonanceK * bp - lp;
                if (filterType == 2) filtered = bp;
                if (filterType == 3) filtered = input - resonanceK * bp; // true notch = LP + HP
            }
            float amp = filtered * v.env * v.velGain * v.unisonScale * layerGain * ampLfo * v.modAmpMul;

            // Attack transient (PCM-style partial, procedural): added after the filter and
            // outside the amplitude contour, so a slow pad keeps an immediate attack.
            if (v.transEnv > 1e-4f) {
                const float e = v.transEnv;
                const double tp = v.transPhase;
                auto partial = [&](double ratio) -> float {
                    if (inc * ratio > .45) return 0.f;
                    const double ph = tp * ratio;
                    return fastSin(2.f * PI_F * float(ph - std::floor(ph)));
                };
                auto bandNoise = [&](float coef) -> float {
                    // Chamberlin band-pass, q = 0.6
                    const float in = lcgFloat(v.noiseSeed);
                    v.transBp1 += coef * v.transBp2;
                    const float hp = in - v.transBp1 - .6f * v.transBp2;
                    v.transBp2 += coef * hp;
                    return v.transBp2;
                };
                float t = 0.f;
                switch (transientType) {
                    case 0: t = bandNoise(noiseBandCoef) * 1.6f * e; break;                         // BREATH
                    case 1: t = partial(1.0) * e + .5f * partial(3.93) * e * e + .25f * partial(9.6) * e * e * e; break; // MALLET
                    case 2: {                                                                         // PLUCK
                        v.transLp += noiseLowCoef * (lcgFloat(v.noiseSeed) - v.transLp);
                        t = (.7f * v.transLp * e + .5f * partial(1.0)) * e;
                        break;
                    }
                    case 3: t = (bandNoise(std::min(.95f, 2.f * PI_F * float(inc) * 3.f)) * 1.4f + .4f * partial(2.0)) * e * e; break; // CHIFF
                    case 4: { const float w = lcgFloat(v.noiseSeed); t = (w - v.transLp) * e * e; v.transLp = w; break; } // CLICK
                    case 5: t = (.7f * partial(2.76) + .45f * partial(5.40) * e + .3f * partial(8.93) * e) * e; break;    // GLASS
                    case 6: t = bandNoise(std::min(.95f, 2.f * PI_F * float(inc) * 2.f)) * 2.f * e * (1.f - e) * 4.f; break; // BOW
                    case 7: t = fastSin(2.f * PI_F * float(tp - std::floor(tp)) + 5.f * e * partial(3.5)) * e; break;   // SPIT (FM)
                    default: break;
                }
                v.transEnv *= transDecayCoef;
                v.transPhase += inc;
                amp += t * transientLevel * v.velGain * v.unisonScale * layerGain * ampLfo;
            }
            if (v.fastRelease) {
                v.killFade -= killStep;
                if (v.killFade <= 0.f) { v.killFade = 0.f; v.active = false; }
                amp *= v.killFade;
            }
            const float outL=amp*v.panGainL;
            const float outR=amp*v.panGainR;
            v.lastOutputL=outL;
            v.lastOutputR=outR;
            sumL += outL;
            sumR += outR;
            if(v.stealFrames>0){
                const float tail=float(v.stealFrames)/float(std::max(1,v.stealTotal));
                sumL+=v.stealCarryL*tail;
                sumR+=v.stealCarryR*tail;
                --v.stealFrames;
            }
        }
        mixL[n] += sumL;
        mixR[n] += sumR;
    }
    if(lfo){lfo->phaseAccum=lfoPhase;lfo->currentVal=lastLfoValue;}
}

void StageSynthModule::init(float sr) {
    initFastMath();   // v32.4: build the sine table off the audio thread
    sr_ = (sr > 8000.0f) ? sr : 48000.0f;
    Dx7LayerEngine::initTables(sr_);
    for (auto& l : layers) l.init(sr_);
    for (auto& fx : layerInsertDSP) fx.init(sr_);
    insertTailActive_.fill(false);insertSilentFrames_.fill(0);
    // Prepared outside the callback for up to 8192 frames per audio block.
    bufL_.assign(8192, 0.0f);
    masterRamp_.assign(8192, 0.0f);
    bufR_.assign(8192, 0.0f);
    generateDefaultWavetables();
    loadBuiltInSynthPreset(synthPresetId);
}

namespace {
void buildWavetableMip(Wavetable& wt){
    constexpr int N=SYNTH_WAVETABLE_SIZE;
    // DFT offline; the audio callback uses only two interpolated lookups.
    std::array<float,N/2+1> real{},imag{};
    for(int k=0;k<=N/2;k++){
        for(int n=0;n<N;n++){
            const float theta=2.f*PI_F*float(k*n)/N;
            real[k]+=wt.data[n]*std::cos(theta)/N;
            imag[k]-=wt.data[n]*std::sin(theta)/N;
        }
    }
    for(int level=0;level<8;level++){
        const int harmonics=std::max(1,128>>level);
        for(int n=0;n<N;n++){
            float v=real[0];
            for(int k=1;k<=harmonics;k++){
                const float theta=2.f*PI_F*float(k*n)/N;
                v+=((k==N/2)?1.f:2.f)*(real[k]*std::cos(theta)-imag[k]*std::sin(theta));
            }
            wt.mip[level][n]=v;
        }
    }
}
}
void StageSynthModule::generateDefaultWavetables() {
    std::lock_guard<std::mutex> lock(wtMutex);
    wavetableBank.clear();
    // First six IDs remain compatible with old user programs; additional spectra
    // enable new morphing sounds with up to 64 harmonics per table.
    const char* names[] = {"Sine","Triangle","Square","Saw","Organ","Bell",
        "Warm Pulse","Shimmer","Glass","Vowel A","Vowel O","Metal",
        "Analog Formant","Hollow","Octave","Bright Strings","Soft Reed",
        "Lunar","Crystal","Nasal","Spectrum","Fifth","Harmonic","Motion",
        "Vox Bloom","PWM Silk","Celesta","Reese Spectrum","Reed Brass","Digital Choir",
        "Sparkle","Reso Motion","Glass Reed","Soft Mallet","Aero Pad","Acid Formant",
        // v31: pulse widths, voices and keys for the LA hybrid and vintage designs.
        "Pulse 25","Pulse 12","Choir Ahh","Choir Ooh","Clav","Harpsichord",
        "Vibes","Breath Flute","Digital Bass","E-Piano Tine","Nasal Brass","Hollow Square"};
    constexpr int kFactoryTables = 48;
    for(int shape=0;shape<kFactoryTables;shape++){
        Wavetable wt;wt.name=names[shape];
        for(int n=0;n<SYNTH_WAVETABLE_SIZE;n++){
            const float ph=2.f*PI_F*float(n)/SYNTH_WAVETABLE_SIZE;
            float value=0.f;
            for(int h=1;h<=64;h++){
                float gain=0.f;
                const float x=float(h);
                switch(shape){
                    case 0: gain=h==1?1.f:0.f;break;
                    case 1: gain=h%2?((h%4==1?1.f:-1.f)/(x*x)):0.f;break;
                    case 2: gain=h%2?1.f/x:0.f;break;
                    case 3: gain=(h%2?1.f:-1.f)/x;break;
                    case 4: gain=h==1?.70f:h==3?.32f:h==5?.22f:h==7?.12f:0.f;break;
                    case 5: gain=h==1?.65f:h==3?.25f:h==6?.40f:h==9?.16f:0.f;break;
                    case 6: gain=h%2?1.f/std::pow(x,1.3f):.06f/x;break;
                    case 7: gain=.85f/x*std::sin(x*.87f);break;
                    case 8: gain=h==1?.62f:h==4?.28f:h==8?.24f:h==11?.14f:0.f;break;
                    case 9: gain=std::exp(-std::pow((x-5.f)/2.4f,2.f))*.28f+.36f/x;break;
                    case 10: gain=std::exp(-std::pow((x-3.f)/1.7f,2.f))*.36f+.33f/x;break;
                    case 11: gain=h%3==0?.7f/x:.16f/x;break;
                    case 12: gain=std::exp(-std::pow((x-9.f)/4.f,2.f))*.13f+.3f/x;break;
                    case 13: gain=h%2?.45f/x:.11f/x;break;
                    case 14: gain=h==1?.63f:h==2?.48f:h==4?.26f:0.f;break;
                    case 15: gain=.75f/x*(1.f+.5f*std::sin(x*.61f));break;
                    case 16: gain=.9f/(x*x*.6f+1.f);break;
                    case 17: gain=.7f/x*std::cos(x*.31f);break;
                    case 18: gain=h==1?.46f:h==5?.40f:h==10?.22f:h==13?.15f:0.f;break;
                    case 19: gain=std::exp(-std::pow((x-7.f)/3.f,2.f))*.26f+.2f/x;break;
                    case 20: gain=(h%5<2?1.f:-1.f)*.67f/x;break;
                    case 21: gain=h==1?.57f:h==3?.26f:h==5?.36f:0.f;break;
                    case 22: gain=.73f/x*(1.f+.36f*std::sin(x*1.73f));break;
                    case 23: gain=.85f/x*std::sin(x*.37f+.5f);break;
                    case 24: gain=.7f*std::exp(-std::pow((x-3.f)/2.2f,2.f))+.2f/x;break;
                    case 25: gain=h%2?.73f/std::pow(x,1.2f):.04f/x;break;
                    case 26: gain=h==1?.85f:h==4?.35f:h==9?.22f:0.f;break;
                    case 27: gain=.7f/x*(1.f+.4f*std::cos(x*.33f));break;
                    case 28: gain=.78f/(x*.35f+1.f)*std::sin(x*.71f);break;
                    case 29: gain=h%3==1?.75f/x:.15f/x;break;
                    case 30: gain=h==1?.65f:h==5?.3f:h==11?.21f:0.f;break;
                    case 31: gain=std::exp(-std::pow((x-9.f)/4.f,2.f))*.25f+.2f/x;break;
                    case 32: gain=.85f/x*std::cos(x*.43f);break;
                    case 33: gain=h==1?.8f:h==2?.25f:h==6?.17f:0.f;break;
                    case 34: gain=.45f/x+.25f*std::exp(-std::pow((x-4.f)/3.f,2.f));break;
                    case 35: gain=h%3==0?.66f/x:.13f/x;break;
                    case 36: gain=std::sin(PI_F*x*.25f)/x;break;
                    case 37: gain=std::sin(PI_F*x*.125f)/x;break;
                    case 38: gain=.5f*std::exp(-std::pow((x-5.f)/1.5f,2.f))+.35f*std::exp(-std::pow((x-9.f)/2.f,2.f))
                                 +.12f*std::exp(-std::pow((x-19.f)/3.f,2.f))+.15f/x;break;
                    case 39: gain=.6f*std::exp(-std::pow((x-3.f)/1.2f,2.f))+.2f*std::exp(-std::pow((x-6.5f)/1.5f,2.f))+.08f/x;break;
                    case 40: gain=1.6f/x*std::abs(std::sin(PI_F*x*.17f));break;
                    case 41: gain=.8f*std::abs(std::sin(PI_F*x*.09f))/std::sqrt(x);break;
                    case 42: gain=h==1?.8f:h==4?.35f:h==10?.15f:0.f;break;
                    case 43: gain=h==1?.9f:h==2?.12f:h==3?.08f:.02f/x;break;
                    case 44: gain=h==1?.8f:h==2?.6f:h==3?.3f:h==4?.1f:0.f;break;
                    case 45: gain=h==1?.85f:h==2?.25f:h==7?.18f:h==14?.1f:0.f;break;
                    case 46: gain=.5f*std::exp(-std::pow((x-8.f)/3.f,2.f))+.45f/x;break;
                    case 47: gain=h%2?(h==3?.2f:1.f)/std::pow(x,.8f):0.f;break;
                    default: gain=0.f;break;
                }
                value+=gain*std::sin(ph*h);
            }
            wt.data[n]=value;
        }
        float peak=.001f;for(float v:wt.data)peak=std::max(peak,std::abs(v));
        for(float& v:wt.data)v*=.85f/peak;
        buildWavetableMip(wt);
        wavetableBank.push_back(std::move(wt));
    }
    wavetables_.publish(std::make_shared<const std::vector<Wavetable>>(wavetableBank));
}

void StageSynthModule::addWavetable(const Wavetable& wt) {
    Wavetable prepared=wt;
    buildWavetableMip(prepared);
    std::lock_guard<std::mutex> lock(wtMutex);
    if (wavetableBank.size()<64){
        wavetableBank.push_back(std::move(prepared));
        wavetables_.publish(std::make_shared<const std::vector<Wavetable>>(wavetableBank));
    }
}

void StageSynthModule::clearWavetables() {
    std::lock_guard<std::mutex> lock(wtMutex);
    wavetableBank.clear();
    wavetables_.publish(std::make_shared<const std::vector<Wavetable>>(wavetableBank));
}

void StageSynthModule::setLFO(int idx, const SynthLFO& lfo) {
    if (idx >= 0 && idx < SYNTH_NUM_LFOS) lfos[idx] = lfo;
}

void StageSynthModule::setEnvelope(int idx, const SynthEnvelope& env) {
    if (idx >= 0 && idx < SYNTH_NUM_ENVS) envelopes[idx] = env;
}

void StageSynthModule::updateLFOsAndEnvs(unsigned int numFrames, float sr) {
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

// V15 original 256-program factory: 16 distinct sound families x 16 variations.
// Every patch is generated from scratch and may use 1-4 truly independent layers.
void StageSynthModule::loadBuiltInSynthPreset(int presetId) {
    synthPresetId = std::clamp(presetId,1,SYNTH_FACTORY_PRESET_COUNT);
    const int family=(synthPresetId-1)/16;
    const int variation=(synthPresetId-1)%16;
    const float tone=float(variation)/15.f;
    const int activeLayers=1+(variation%4);
    synthMasterVol=.82f;
    synthMacroCutoff=1.f;
    synthWheelGlobalCutoff=0.f;synthWheelGlobalAmpDb=0.f;synthOutGainDb=0.f;
    // Lead family uses each layer's mono/legato and portamento settings.
    // Other families retain the global polyphonic master behavior.
    synthVoiceMaster=(family!=4);
    synthVoiceMode=0;
    synthPortamentoEnabled=false;
    synthPortamento=0.f;
    synthGlideMode=1;
    synthSlide=0.f;
    seqPhase_.fill(0.0);seqAmp_.fill(1.f);
    for(int layer=0;layer<4;layer++){
        auto& c=layers[layer].cfg;
        c=SynthLayerConfig{};
        c.enabled=layer<activeLayers;
        auto& lfo=lfos[layer];
        lfo=SynthLFO{}; // never inherit the previous patch's modulation phase/routing
        lfo.enabled=c.enabled && ((family+variation+layer)%5==0);
        lfo.waveType=(family+layer)%3;
        lfo.rateHz=0.20f+float((family+variation+layer*3)%17)*.21f;
        lfo.depth=.28f+float((variation+layer)%5)*.09f;
        lfo.destPitch=((family+layer)%4==0)?22:0;
        lfo.destCutoff=((family+layer)%3==1)?(240+60*layer):0;
        lfo.destVolume=((family+layer)%7==2)?.11f:0.f;
        lfo.destPan=((family+layer)%7==4)?.17f:0.f;
        lfo.phaseAccum=double(layer)*.125;

        c.volume=std::clamp((.72f-.10f*layer)/std::sqrt(float(activeLayers)),.20f,.85f);
        c.pan=layer%2 ? (.18f+.15f*tone) : (-.18f-.15f*tone);
        if(activeLayers==1)c.pan=0.f;
        c.detuneCents=(layer%2?1.f:-1.f)*(3.f+float(variation%7)*1.5f);
        c.cutoffHz=std::clamp(900.f+float((variation*773+layer*1193+family*283)%11000),200.f,15000.f);
        c.resonanceQ=.78f+.12f*float((variation+layer)%8);
        c.filterEnvAmt=.10f+.06f*float((variation+layer)%7);
        c.velToAmp=1.f;c.highpassHz=20.f;
        c.unisonVoices=(family==4||family==10)?2:1;
        c.unisonDetune=8.f+variation*1.25f;
        c.unisonSpread=.42f+.18f*tone;
        c.phaseOffset=float((variation*3+layer)%13)/13.f;
        c.attackSec=.004f+.001f*layer;
        c.decaySec=.7f;c.sustainLevel=.7f;c.releaseSec=.45f;
        // Deliberate oscillator and spectral differences even inside one family.
        // Factory performance routes are deliberately useful at any CC1 position;
        // independent layer targets produce filter plucks, evolving pads and leads.
        c.wheelVibratoCents=(family==4||family==14)?(15.f+variation*3.f):30.f;
        c.wheelMode=(family==4||family==14)?0:2; // custom routes + familiar vibrato where applicable
        c.wheelVibratoRateHz=4.2f+.19f*(variation%12);
        c.wheelCutoffOctaves=(family==13 || family==2)?(-3.f+.18f*variation):
            (family==0||family==1||family==8||family==9||family==14)?(1.2f+layer*.3f):0.f;
        c.wheelAmpDb=(family==13 && layer==0)?-5.f:(family==8?3.f:0.f);
        c.wheelWtMorph=(family==8||family==9||family==14)?(.35f+.08f*(layer%3)):0.f;
        c.waveType=(variation+layer)%2?8:9;
        c.pulseWidth=.30f+.03f*float((variation+layer)%9);
        c.pwmDepth=.08f+.015f*(variation%10);
        c.pwmRateHz=.21f+.06f*((variation+layer)%10);
        c.sawLevel=.48f+.025f*(variation%10);
        c.pulseLevel=.37f;c.subLevel=.13f;
        c.osc2Level=.2f+.035f*(variation%8);
        c.osc2Detune=5.f+float(variation)*.9f;
        c.wavetableIdx=(variation*3+layer*5+family)%24;
        c.wavetableMorph=.10f+.052f*float((variation+layer)%16);
        c.fmIndex=.7f+.19f*variation+.24f*layer;
        c.fmRatio=.5f+.37f*float((variation+layer*3)%12);
        for(int effect=0;effect<5;effect++){
            auto& f=c.insertFx[effect];
            f.enabled=false;
            f.rateHz=.16f+.075f*float((variation+layer+effect)%15);
            f.depth=.26f+.043f*float((variation+effect)%13);
            f.mix=.14f+.018f*float((variation+layer)%11);
            f.feedback=(effect==4?.25f:.075f)+.013f*float((variation+effect)%8);
            f.timeMs=105.f+16.f*float((variation+layer*2)%18);
        }
        if(!c.enabled)continue;
        switch(family){
          case 0: // warm DCO pads, first chorus algorithm
          case 1: // ensemble strings, second chorus algorithm
            c.waveType=family==0?8:9;
            c.attackSec=.10f+.16f*tone+layer*.035f;
            c.decaySec=1.2f+.35f*tone;c.sustainLevel=.80f;
            c.releaseSec=1.3f+2.1f*tone;
            c.cutoffHz=1300.f+tone*5000.f+layer*480.f;
            c.insertFx[family==0?0:1].enabled=true;
            c.insertFx[family==0?0:1].mix=.29f+.13f*tone;
            c.insertFx[family==0?0:1].depth=.40f+.40f*tone;
            if(variation%5==0)c.insertFx[4].enabled=true;
            break;
          case 2: // playable analog poly keys
            c.waveType=(variation%3==0)?1:9;
            c.attackSec=.003f;c.decaySec=.24f+.5f*tone;
            c.sustainLevel=.32f+.35f*tone;c.releaseSec=.3f+.35f*tone;
            c.cutoffHz=1600.f+8400.f*tone;
            c.insertFx[variation%2].enabled=true;
            break;
          case 3: // filter-swept dual VCO brass
            c.waveType=9;c.osc2Level=.5f;
            c.cutoffHz=630.f+2000.f*tone;
            c.filterEnvAmt=.65f+.3f*tone;
            c.filtDecaySec=.12f+.48f*tone;
            c.attackSec=.014f;c.sustainLevel=.54f;
            c.insertFx[2+(variation%2)].enabled=variation%3==0;
            break;
          case 4: // mono/legato stage leads
            c.waveType=(variation%3==0)?7:((variation%3==1)?9:14);
            c.attackSec=.002f;c.sustainLevel=.8f;
            c.cutoffHz=1600.f+10000.f*tone;c.releaseSec=.13f+.2f*tone;
            c.drive=.05f+.25f*tone;
            c.voiceMode=1;c.portamentoEnabled=variation%2==0;
            c.portamento=.035f+.23f*tone;
            c.insertFx[(variation%2)?3:2].enabled=true;
            break;
          case 5: // bold sub bass sounds
            c.waveType=variation%3==0?11:8;
            c.octave=layer==0?-1:0;
            c.subLevel=.6f;c.osc2Level=.08f;
            c.cutoffHz=260.f+2400.f*tone;
            c.attackSec=.002f;c.decaySec=.12f+.5f*tone;
            c.sustainLevel=.42f+.28f*tone;c.releaseSec=.1f+.17f*tone;
            c.insertFx[2].enabled=variation%4==0;
            break;
          case 6: // electric FM / struck key
          case 7: // FM metallic bells
            c.synthesisType=1;c.waveType=10;
            c.fmRatio=family==6?(.5f+.5f*(variation%8)):(1.3f+.71f*(variation%11));
            c.fmIndex=.75f+.4f*(variation%9);
            c.attackSec=.002f;c.decaySec=.24f+.11f*(variation%9);
            c.sustainLevel=family==6?.32f:.07f;c.releaseSec=.6f+.22f*layer;
            c.cutoffHz=3000.f+float(variation)*530.f;
            if(variation%4==0)c.insertFx[4].enabled=true;
            break;
          case 8: // airy scanned wavetable pads
          case 9: // morphing digital motion
            c.synthesisType=2;c.waveType=6;
            c.wavetableIdx=(4+variation+layer*3+family)%24;
            c.wavetableMorph=.05f+.057f*float(variation);
            c.attackSec=.14f+tone*.6f;c.decaySec=1.5f;
            c.sustainLevel=.84f;c.releaseSec=1.7f+2.f*tone;
            c.cutoffHz=2000.f+6800.f*tone;
            c.insertFx[family==8?1:3].enabled=true;
            c.insertFx[4].enabled=variation%3==0;
            break;
          case 10: // step-sequenced saws
          case 11: // bass sequencers
          case 12: // sequenced poly textures
            c.synthesisType=family==12?2:0;
            c.waveType=family==11?8:4;
            c.seqEnabled=layer<=1;c.seqBpm=85.f+variation*9.f;
            c.seqSteps=variation%3==0?12:16;
            c.seqGate=.32f+.03f*(variation%12);
            for(int step=0;step<16;step++){
                const int pattern[8]={0,3,7,12,10,7,5,2};
                c.seqNotes[step]=((step+variation)%11==9)?99:pattern[(step+variation*3+layer)%8]-((family==11)?12:0);
            }
            c.attackSec=.003f;c.releaseSec=.11f+.02f*(variation%6);
            c.cutoffHz=900.f+variation*650.f;
            c.insertFx[4].enabled=true;
            c.insertFx[4].mix=.12f;c.insertFx[4].feedback=.20f;
            break;
          case 13: // snappy keys & plucks
            c.synthesisType=variation%3==0?2:0;
            c.waveType=variation%2?1:6;
            c.attackSec=.002f;c.decaySec=.11f+.07f*(variation%8);
            c.sustainLevel=.1f+.04f*(variation%4);c.releaseSec=.18f+.07f*(variation%6);
            c.cutoffHz=2600.f+variation*640.f;
            if(variation%2)c.insertFx[0].enabled=true;
            break;
          case 14: // evolving, expressive wavetable leads
            c.synthesisType=2;c.waveType=6;
            c.wavetableIdx=(variation*2+layer*5+8)%24;
            c.wavetableMorph=.12f+tone*.84f;
            c.attackSec=.005f;c.releaseSec=.22f+tone*.5f;
            c.cutoffHz=2000.f+8500.f*tone;
            c.insertFx[2+(variation%2)].enabled=true;
            break;
          default: // cinematic hybrid stacks
            c.synthesisType=layer%3==0?2:layer%3==1?1:0;
            c.waveType=layer%2?10:8;
            c.attackSec=layer==0?.006f:.15f+tone*.5f;
            c.releaseSec=1.f+tone*3.f;
            c.cutoffHz=1600.f+tone*8000.f;
            c.insertFx[layer%2].enabled=true;
            if(variation%3==0)c.insertFx[4].enabled=true;
            break;
        }
    }
    for(auto& layer:layers){layer.heldKeys.clear();layer.reset();}
    for(auto& fx:layerInsertDSP)fx.reset();
    insertTailActive_.fill(false);insertSilentFrames_.fill(0);
}

void StageSynthModule::noteOn(int rawKey, int playedKey, int velocity, unsigned layerMask) {
    randomSeed_ = randomSeed_ * 1664525u + 1013904223u;
    modRandom = float(randomSeed_ >> 8) / float(1u << 24) * 2.f - 1.f;
    if (!synthEnabled || rawKey < synthKeyLow || rawKey > synthKeyHigh) return;
    for (size_t index=0;index<layers.size();++index) {
        auto& layer=layers[index];
        const auto& c=layer.cfg;
        if (!(layerMask & (1u << index))) continue;              // v33: not routed to this layer
        if (!c.enabled || rawKey < c.keyLow || rawKey > c.keyHigh) continue;
        if(c.seqEnabled){
            // SYNC CLOCK: one key restarts every sequenced layer together.
            if(synthSeqGlobalClock){ for(size_t k=0;k<layers.size();++k)if(layers[k].cfg.seqEnabled)seqPhase_[k]=0.0; }
            else seqPhase_[index]=0.0;
        }
        layer.voiceMode = synthVoiceMaster ? synthVoiceMode : c.voiceMode;
        layer.glideSeconds = synthVoiceMaster ? synthPortamento : c.portamento;
        layer.glideEnabled = synthVoiceMaster ? synthPortamentoEnabled : c.portamentoEnabled;
        layer.glideMode = synthVoiceMaster ? synthGlideMode : c.glideMode;
        layer.slideSemis = synthVoiceMaster ? synthSlide : c.slide;
        if (layer.voiceMode != 0) {
            auto& held=layer.heldKeys;
            held.erase(std::remove_if(held.begin(),held.end(),[rawKey](const auto& n){return n[0]==rawKey;}),held.end());
            held.push_back({rawKey,playedKey,velocity});
        }
        layer.noteOn(rawKey, std::clamp(playedKey+c.keyTranspose,0,127), velocity);
    }
}

void StageSynthModule::noteOff(int rawKey, bool pedalDown, unsigned layerMask) {
    for (size_t index=0;index<layers.size();++index) {
        if (!(layerMask & (1u << index))) continue;              // v33: only the layers the note went to
        auto& layer=layers[index];
        if (layer.voiceMode!=0) {
            auto& held=layer.heldKeys;
            const bool wasCurrent=!held.empty() && held.back()[0]==rawKey;
            held.erase(std::remove_if(held.begin(),held.end(),[rawKey](const auto& n){return n[0]==rawKey;}),held.end());
            if (!wasCurrent) continue;
            if (!held.empty()) {
                const auto n=held.back();
                layer.noteOn(n[0],std::clamp(n[1]+layer.cfg.keyTranspose,0,127),n[2]);
                continue;
            }
        }
        layer.noteOff(rawKey,pedalDown);
    }
}

void StageSynthModule::setSustain(bool down) {
    for (auto& l : layers) l.setSustain(down);
}

void StageSynthModule::allNotesOff() {
    // v32: every voice fades out in 12 ms (no hard cut, no click). Insert tails
    // (echo, reverb) ring out naturally. Must run on the audio thread: the host
    // queues it (StagePianoEngine::queueSynthAllNotesOff) from UI / HTTP threads.
    monoHeldNotes.clear();
    for(auto& layer:layers)layer.releaseAll();
}

void StageSynthModule::render(float* outInterleavedStereo, unsigned int numFrames,
                             const std::array<float*, SYNTH_LAYER_COUNT>* layerStems) {
    AudioEpoch::Scope epochScope{audioEpoch_};
    if (!needsRender()) {
        masterSmooth_ = 0.f;
        return;
    }
    // Avoid allocating inside the real-time audio callback.
    // init() reserves 8192 frames; larger host blocks are split in place.
    if(numFrames>8192){
        for(unsigned int off=0;off<numFrames;off+=8192){
            std::array<float*,SYNTH_LAYER_COUNT> chunkStems{};
            if(layerStems)for(int i=0;i<SYNTH_LAYER_COUNT;++i)chunkStems[i]=(*layerStems)[i]+size_t(off)*2;
            render(outInterleavedStereo+size_t(off)*2,std::min(8192u,numFrames-off),
                   layerStems?&chunkStems:nullptr);
        }
        return;
    }
    const auto* immutableTables=wavetables_.audio();
    const auto& tables=immutableTables ? *immutableTables : wavetableBank;
    // Master level and module ON/OFF: one 10 ms ramp per block, shared by all layers.
    const float smoothK = 1.0f - std::exp(-1.0f / (0.010f * sr_));
    const bool held = patchHold_.load(std::memory_order_acquire) > 0;
    if (seqRestart_.exchange(false, std::memory_order_acq_rel)) { seqPhase_.fill(0.0); seqAmp_.fill(1.f); }
    const float masterTarget = (synthEnabled && !held) ? std::max(0.f, synthMasterVol) * std::pow(10.f, std::clamp(synthOutGainDb, -24.f, 24.f) / 20.f) : 0.f;
    if (masterRamp_.size() < numFrames) masterRamp_.resize(8192);   // init() reserves 8192
    // A patch-change hold fades out faster (2 ms time constant: silent in < 20 ms).
    const float masterK = held ? 1.0f - std::exp(-1.0f / (0.002f * sr_)) : smoothK;
    for (unsigned int n = 0; n < numFrames; ++n) {
        masterSmooth_ += (masterTarget - masterSmooth_) * masterK;
        if (masterTarget <= 0.f && masterSmooth_ < 1e-4f) masterSmooth_ = 0.f;   // below -80 dB: done
        masterRamp_[n] = masterSmooth_;
    }
    masterLevel_.store(masterSmooth_, std::memory_order_release);
    renderBlocks_.fetch_add(1, std::memory_order_relaxed);
    renderThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    lastRenderNs_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
    // Faded out for a patch change: keep the last consistent config (no half-written copies).
    const bool freezeConfig = held && masterSmooth_ <= 0.f;
    // Per-layer LFO phases are advanced inside renderLayer() at audio rate.
    // ---- v32.3 modulation: global source values, then the per-block copy of every layer.
    const ModMatrixSnapshot* modSnap = modMatrix_.audio();
    std::array<float, MOD_SOURCE_COUNT> modGlobal{};
    modGlobal[MOD_WHEEL] = std::clamp(layers[0].performanceModWheel, 0.f, 1.f);
    modGlobal[MOD_BEND] = std::clamp(modBend, -1.f, 1.f);
    modGlobal[MOD_AFTERTOUCH] = std::clamp(modAftertouch, 0.f, 1.f);
    for (int i = 0; i < 4; ++i) modGlobal[size_t(MOD_LFO1 + i)] = std::clamp(lfos[size_t(i)].currentVal, -1.f, 1.f);
    modGlobal[MOD_RANDOM] = modRandom;
    for (int i = 0; i < 4; ++i) modGlobal[size_t(MOD_MACRO1 + i)] = std::clamp(macros[size_t(i)], 0.f, 1.f);
    const auto& targets = modTargets();
    for (int l = 0; l < SYNTH_LAYER_COUNT; ++l) {
        auto& layer = layers[size_t(l)];
        if (!freezeConfig) layer.runCfg = layer.cfg;
        layer.index = l;
        layer.modSnap = modSnap;
        layer.modGlobal = modGlobal;
    }
    if (modSnap) for (int k = 0; k < modSnap->count; ++k) {
        const auto& c = modSnap->list[size_t(k)];
        if (c.voice != MVT_NONE || modSourceIsVoice(c.source) || c.layer < 0 || c.target < 0) continue;
        const auto& d = targets[size_t(c.target)];
        auto& rc = layers[size_t(c.layer)].runCfg;
        const float m = c.amount * modShape(modGlobal[size_t(c.source)], modSourceIsBipolar(c.source), c.bipolar, c.curve);
        if (d.field) {
            rc.*(d.field) = modDenormalize(d, modNormalize(d, rc.*(d.field)) + m);
        } else if (d.insertUnit >= 0) {
            auto& u = rc.insertFx[size_t(d.insertUnit)];
            float* f = d.insertField == 0 ? &u.mix : d.insertField == 1 ? &u.rateHz : d.insertField == 2 ? &u.depth : &u.feedback;
            *f = modDenormalize(d, modNormalize(d, *f) + m);
        }
    }
    const int permitted=std::clamp(voiceLimitPerLayer,1,SYNTH_MAX_VOICES_PER_LAYER);
    for (int layer = 0; layer < SYNTH_LAYER_COUNT; ++layer) {
        // v32: a smaller shared budget only limits NEW notes; sounding voices above
        // it finish naturally (cutting them was an audible click).
        layers[layer].voiceLimit=permitted;
        const auto& item=layers[layer];
        // Layer ON/OFF fades in 10 ms; once silent, its voices are released.
        const float gateTarget = item.cfg.enabled ? 1.f : 0.f;
        float& gate = layerGate_[size_t(layer)];
        if (gateTarget <= 0.f && gate <= 1e-5f) {
            gate = 0.f;
            layers[layer].renderWhileDisabled = false;
            for (auto& v : layers[layer].voices) if (v.active) { v.active = false; v.keyHeld = false; v.sustained = false; }
            continue;
        }
        layers[layer].renderWhileDisabled = gateTarget <= 0.f;
        if (item.cfg.volume <= 0.001f) continue;
        const bool hasVoices=std::any_of(item.voices.begin(),item.voices.end(),
                         [](const SynthPolyVoice& voice){return voice.active;});
        const bool hasFx=std::any_of(item.runCfg.insertFx.begin(),item.runCfg.insertFx.end(),
                         [](const SynthLayerFxParams& fx){return fx.enabled && fx.mix>0.0001f;});
        const bool insertFading=layerInsertDSP[layer].busy();
        if (!hasVoices && !insertFading && (!hasFx || !insertTailActive_[layer])) { gate = gateTarget; continue; }
        if (hasVoices){insertTailActive_[layer]=hasFx;insertSilentFrames_[layer]=0;}
        std::fill(bufL_.begin(), bufL_.begin() + numFrames, 0.0f);
        std::fill(bufR_.begin(), bufR_.begin() + numFrames, 0.0f);
        // Render sequenced layers in short segments: no allocations or locks on audio thread.
        // At each step transition, target pitches track held keys, including layer transpose.
        const auto& seq=layers[layer].cfg;
        // SHARE PATTERN: layers B-D play layer A's steps, length, gate, accents and slides.
        const auto& pat=synthSeqSharePattern ? layers[0].cfg : seq;
        if(seq.seqEnabled && hasVoices){
            // Tempo: master BPM with SYNC CLOCK or the layer's SYNC, else its own BPM.
            static const double stepsPerBeat[6]={1.0,2.0,3.0,4.0,6.0,8.0};
            const double bpm=(synthSeqGlobalClock||seq.seqSync)?std::clamp(double(masterBpm),30.0,300.0):std::clamp(double(seq.seqBpm),30.0,300.0);
            const double samplesPerStep=std::max(1.0,double(sr_)*60.0/(bpm*stepsPerBeat[std::clamp(seq.seqDivision,0,5)]));
            unsigned int offset=0;
            while(offset<numFrames){
                const int length=std::clamp(pat.seqSteps,1,16);
                const int index=int(seqPhase_[layer]/samplesPerStep)%length;
                const int prevIndex=(index+length-1)%length;
                const int semis=pat.seqNotes[index];
                const bool accent=pat.seqAccent[size_t(index)]!=0;
                const bool slideIn=pat.seqSlide[size_t(prevIndex)]!=0 && pat.seqNotes[prevIndex]!=99;   // glide into this step
                const bool slideOut=pat.seqSlide[size_t(index)]!=0;                                     // hold the gate to the next step
                seqStep_[layer]=index;
                const double frac=seqPhase_[layer]-std::floor(seqPhase_[layer]/samplesPerStep)*samplesPerStep;
                const unsigned int take=std::min(numFrames-offset,static_cast<unsigned int>(std::max(1.,std::ceil(samplesPerStep-frac))));
                layers[layer].seqGlideSeconds=slideIn?float(std::min(.08,samplesPerStep/sr_*.45)):0.f;
                for(auto& v:layers[layer].voices)if(v.active && v.keyHeld){
                    const double pitch=60.0+(v.playedKey-60.0)*seq.keyTrack + seq.octave*12.0 + seq.semitones+seq.detuneCents/100.0;
                    v.targetFreq=440.0*std::pow(2.0,(pitch+(semis==99?0:semis)-69.0)/12.0);
                }
                layers[layer].renderLayer(bufL_.data()+offset,bufR_.data()+offset,take,sr_,
                                         synthMacroCutoff,tables,&lfos[layer],envelopes.data());
                // A short ramp suppresses click transients at step and gate transitions.
                for(unsigned int n=0;n<take;++n){
                    const double at=frac+n;
                    const bool gateOpen=slideOut || at<samplesPerStep*std::clamp(double(pat.seqGate),0.05,1.0);
                    const float desired=(semis==99||!gateOpen)?0.f:(accent?1.35f:1.f);
                    seqAmp_[layer]+=(desired-seqAmp_[layer])*.006f;
                    bufL_[offset+n]*=seqAmp_[layer];bufR_[offset+n]*=seqAmp_[layer];
                }
                offset+=take;
                seqPhase_[layer]+=take;
                if(seqPhase_[layer]>=samplesPerStep*length)seqPhase_[layer]=std::fmod(seqPhase_[layer],samplesPerStep*length);
            }
            layers[layer].seqGlideSeconds=0.f;
        }else{
            seqPhase_[layer]=0.0;seqAmp_[layer]=1.f;seqStep_[layer]=-1;
            if(hasVoices)layers[layer].renderLayer(bufL_.data(), bufR_.data(), numFrames, sr_,
                                     synthMacroCutoff, tables, &lfos[layer], envelopes.data());
        }
        float* stem = layerStems ? (*layerStems)[layer] : outInterleavedStereo;
        float outputPeak=0.f;
        // v32.4 per-layer output GAIN (after the inserts), smoothed like the master level.
        const float gainTarget=std::pow(10.f,std::clamp(item.runCfg.outGainDb,-24.f,24.f)/20.f);
        float& outGain=outGain_[layer];
        // v32.4 layer EQ, first in the insert chain.
        auto& eqs=layerEq_[layer];
        {
            const auto& c=item.runCfg;
            const bool changed=!eqs.valid||eqs.hp!=c.eqHighPass||eqs.lp!=c.eqLowPass||eqs.hpS!=c.eqHighPassSlope||eqs.lpS!=c.eqLowPassSlope||
                [&]{for(int b=0;b<3;++b)if(eqs.f[b]!=c.eqFreq[size_t(b)]||eqs.q[b]!=c.eqQ[size_t(b)]||eqs.g[b]!=c.eqGain[size_t(b)])return true;return false;}();
            if(changed&&(c.eqEnabled||eqs.gate>0.f)){
                EqBandParams bands[5]{};
                for(int b=0;b<3;++b){bands[b].freqHz=c.eqFreq[size_t(b)];bands[b].q=c.eqQ[size_t(b)];bands[b].gainDb=c.eqGain[size_t(b)];
                    eqs.f[b]=c.eqFreq[size_t(b)];eqs.q[b]=c.eqQ[size_t(b)];eqs.g[b]=c.eqGain[size_t(b)];}
                for(int b=3;b<5;++b){bands[b].freqHz=1000.f;bands[b].q=1.f;bands[b].gainDb=0.f;}
                eqs.eq.update(sr_,true,c.eqHighPass,c.eqLowPass,bands,c.eqHighPassSlope,c.eqLowPassSlope);
                eqs.hp=c.eqHighPass;eqs.lp=c.eqLowPass;eqs.hpS=c.eqHighPassSlope;eqs.lpS=c.eqLowPassSlope;eqs.valid=true;
            }
        }
        const float eqTarget=item.runCfg.eqEnabled?1.f:0.f;
        const bool eqActive=eqTarget>0.f||eqs.gate>0.f;
        for (unsigned int n = 0; n < numFrames; ++n) {
            float sampleL=bufL_[n], sampleR=bufR_[n];
            if(eqActive){
                eqs.gate+=(eqTarget-eqs.gate)*smoothK;
                if(eqTarget<=0.f&&eqs.gate<1e-4f)eqs.gate=0.f;
                float wetL=sampleL,wetR=sampleR;
                eqs.eq.processStereo(wetL,wetR);
                sampleL+=(wetL-sampleL)*eqs.gate;sampleR+=(wetR-sampleR)*eqs.gate;
            }
            if(hasFx||insertFading)layerInsertDSP[layer].process(sampleL,sampleR,item.runCfg.insertFx,masterBpm);
            // Last-resort realtime safety guard for malformed imported patches.
            sampleL=std::isfinite(sampleL)?std::clamp(sampleL,-8.f,8.f):0.f;
            sampleR=std::isfinite(sampleR)?std::clamp(sampleR,-8.f,8.f):0.f;
            outputPeak=std::max(outputPeak,std::max(std::abs(sampleL),std::abs(sampleR)));
            gate += (gateTarget - gate) * smoothK;
            if (gateTarget <= 0.f && gate < 1e-4f) gate = 0.f;
            outGain += (gainTarget - outGain) * smoothK;
            const float level = masterRamp_[n] * gate * outGain;
            stem[n * 2]     += sampleL * level;
            stem[n * 2 + 1] += sampleR * level;
        }
        if(!hasVoices && hasFx){
            // A synchronized delay may have an 8-second first tap. Preserve its
            // pending echoes after note-off instead of truncating the tail at 1s.
            // Non-delay insert chains still release the DSP after 1.5s of silence.
            const float silenceWindowSec = insertTailSeconds(item.runCfg.insertFx);
            if(outputPeak<1e-6f){
                insertSilentFrames_[layer]+=numFrames;
                if(insertSilentFrames_[layer]>=static_cast<unsigned int>(sr_*silenceWindowSec)){
                    insertTailActive_[layer]=false;
                    insertSilentFrames_[layer]=0;
                    // v32: no 3 MB reset() here (an audio-thread spike): units
                    // switched off are cleared incrementally by the chain itself.
                }
            }else insertSilentFrames_[layer]=0;
        }
    }
    if (!synthEnabled && masterSmooth_ < 1e-6f) {
        masterSmooth_ = 0.f;
        for (auto& l : layers) for (auto& v : l.voices) if (v.active) { v.active = false; v.keyHeld = false; v.sustained = false; }
    }
}

} // namespace SonusDSP
// ---------------------------------------------------------------- v32.3 modulation matrix
namespace SonusDSP {
void StageSynthModule::setModMatrix(const std::vector<ModConnectionSpec>& specs) {
    auto snap = std::make_shared<ModMatrixSnapshot>();
    std::vector<ModConnectionSpec> kept;
    for (const auto& spec : specs) {
        if (snap->count >= MOD_MAX_CONNECTIONS) break;
        int layer = -1, target = -1;
        if (spec.source < 0 || spec.source >= MOD_SOURCE_COUNT || !modResolveTarget(spec.target, layer, target)) continue;
        const int voice = modTargets()[size_t(target)].voice;
        if (modSourceIsVoice(spec.source) && voice == MVT_NONE) continue;    // per-voice source needs a voice target
        auto& c = snap->list[size_t(snap->count++)];
        c.source = spec.source; c.layer = layer; c.target = target; c.voice = voice;
        c.amount = std::clamp(spec.amount, -1.f, 1.f); c.bipolar = spec.bipolar; c.curve = std::clamp(spec.curve, 0, 3);
        kept.push_back(spec);
        kept.back().amount = c.amount;
    }
    std::lock_guard<std::mutex> lock(modMutex_);
    modSpecs_ = std::move(kept);
    modMatrix_.publish(std::move(snap));
}

std::vector<ModConnectionSpec> StageSynthModule::getModMatrix() const {
    std::lock_guard<std::mutex> lock(modMutex_);
    return modSpecs_;
}
} // namespace SonusDSP
