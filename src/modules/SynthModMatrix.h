#pragma once
// v32.3 synth modulation matrix (drag a source onto a knob, Vital-style).
//
// A connection = source -> target parameter, with an AMOUNT in the target's normalized
// range: the knob value is where the modulation starts ("from") and knob + amount is
// where it ends ("to"); a bipolar connection swings knob +/- amount. Curves shape the
// source (linear, exponential, logarithmic, S).
//
//  - Global sources (wheel, bend, aftertouch, LFO 1-4, random, macros 1-4) work on any
//    target in the table below. They are applied once per audio block to a COPY of the
//    layer configuration (SingleSynthLayer::runCfg), so the knob value (the base) is
//    never overwritten, saved modulated or lost.
//  - Per-voice sources (velocity, key, amp envelope, filter envelope) work inside each
//    voice on the voice targets: cutoff, level, pan and pitch.
#include "../core/StageTypes.h"
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace SonusDSP {

enum ModSource : int {
    MOD_WHEEL = 0, MOD_BEND, MOD_AFTERTOUCH, MOD_VELOCITY, MOD_KEY, MOD_AMP_ENV, MOD_FILTER_ENV,
    MOD_LFO1, MOD_LFO2, MOD_LFO3, MOD_LFO4, MOD_RANDOM, MOD_MACRO1, MOD_MACRO2, MOD_MACRO3, MOD_MACRO4,
    MOD_SOURCE_COUNT
};
inline bool modSourceIsVoice(int s) { return s == MOD_VELOCITY || s == MOD_KEY || s == MOD_AMP_ENV || s == MOD_FILTER_ENV; }
inline bool modSourceIsBipolar(int s) { return s == MOD_BEND || s == MOD_KEY || (s >= MOD_LFO1 && s <= MOD_LFO4) || s == MOD_RANDOM; }
inline const char* modSourceName(int s) {
    static const char* names[MOD_SOURCE_COUNT] = {"WHEEL", "BEND", "AFTERTOUCH", "VELOCITY", "KEY", "AMP ENV", "FILTER ENV",
        "LFO A", "LFO B", "LFO C", "LFO D", "RANDOM", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4"};
    return s >= 0 && s < MOD_SOURCE_COUNT ? names[s] : "?";
}

/** Voice-level targets (per-voice sources are allowed only on these). */
enum ModVoiceTarget : int { MVT_NONE = 0, MVT_CUTOFF, MVT_LEVEL, MVT_PAN, MVT_PITCH };

/** A modulatable layer parameter: suffix of its registry id ("L1_" + suffix). */
struct ModTargetDef {
    const char* suffix;
    float SynthLayerConfig::* field;   // null for insert-FX targets
    float min, max;
    bool log;
    int voice;                         // ModVoiceTarget
    int insertUnit = -1;               // insert FX target: unit 0-5
    int insertField = 0;               // 0 mix, 1 rate, 2 depth, 3 feedback
};

inline const std::vector<ModTargetDef>& modTargets() {
    using C = SynthLayerConfig;
    static const std::vector<ModTargetDef> table = [] {
        std::vector<ModTargetDef> t = {
            {"cut", &C::cutoffHz, 40.f, 20000.f, true, MVT_CUTOFF},
            {"vol", &C::volume, 0.f, 1.5f, false, MVT_LEVEL},
            {"pan", &C::pan, -1.f, 1.f, false, MVT_PAN},
            {"det", &C::detuneCents, -50.f, 50.f, false, MVT_NONE},
            {"res", &C::resonanceQ, .5f, 12.f, false, MVT_NONE},
            {"filtEnv", &C::filterEnvAmt, -1.f, 1.f, false, MVT_NONE},
            {"filtKT", &C::filterKeyTrack, 0.f, 1.f, false, MVT_NONE},
            {"hpf", &C::highpassHz, 20.f, 6000.f, true, MVT_NONE},
            {"drive", &C::drive, 0.f, 1.f, false, MVT_NONE},
            {"atk", &C::attackSec, .001f, 4.f, true, MVT_NONE},
            {"dec", &C::decaySec, .02f, 6.f, true, MVT_NONE},
            {"sus", &C::sustainLevel, 0.f, 1.f, false, MVT_NONE},
            {"rel", &C::releaseSec, .02f, 6.f, true, MVT_NONE},
            {"fAtk", &C::filtAttackSec, .001f, 4.f, true, MVT_NONE},
            {"fDec", &C::filtDecaySec, .005f, 10.f, true, MVT_NONE},
            {"fSus", &C::filtSustainLvl, 0.f, 1.f, false, MVT_NONE},
            {"fRel", &C::filtReleaseSec, .005f, 10.f, true, MVT_NONE},
            {"saw", &C::sawLevel, 0.f, 1.f, false, MVT_NONE},
            {"pulse", &C::pulseLevel, 0.f, 1.f, false, MVT_NONE},
            {"pw", &C::pulseWidth, .05f, .95f, false, MVT_NONE},
            {"pwmDepth", &C::pwmDepth, 0.f, .45f, false, MVT_NONE},
            {"pwmRate", &C::pwmRateHz, .05f, 12.f, true, MVT_NONE},
            {"sub", &C::subLevel, 0.f, 1.f, false, MVT_NONE},
            {"noise", &C::noiseLevel, 0.f, 1.f, false, MVT_NONE},
            {"osc2", &C::osc2Level, 0.f, 1.f, false, MVT_NONE},
            {"osc2Det", &C::osc2Detune, -50.f, 50.f, false, MVT_NONE},
            {"fmIndex", &C::fmIndex, 0.f, 8.f, false, MVT_NONE},
            {"fmRatio", &C::fmRatio, .25f, 12.f, true, MVT_NONE},
            {"wtMorph", &C::wavetableMorph, 0.f, 1.f, false, MVT_NONE},
            {"uniDet", &C::unisonDetune, 0.f, 50.f, false, MVT_NONE},
            {"uniSprd", &C::unisonSpread, 0.f, 1.f, false, MVT_NONE},
            {"laMix", &C::laMix, 0.f, 1.f, false, MVT_NONE},
            {"laRing", &C::laRing, 0.f, 1.f, false, MVT_NONE},
            {"analog", &C::analog, 0.f, 1.f, false, MVT_NONE},
            {"portamento", &C::portamento, 0.f, 2.f, false, MVT_NONE},
            {"wheelVibrato", &C::wheelVibratoCents, 0.f, 200.f, false, MVT_NONE},
            {"seqGate", &C::seqGate, .05f, 1.f, false, MVT_NONE},
        };
        // Pitch in semitones (the SEMI knob): continuous +/-12 per voice.
        t.push_back({"semi", nullptr, -12.f, 12.f, false, MVT_PITCH});
        // Insert units: L1_fx<u>_mix / _rate / _depth / _feedback
        static const char* fields[4] = {"mix", "rate", "depth", "feedback"};
        static std::vector<std::string> names;
        names.reserve(24);
        for (int u = 0; u < 6; ++u)
            for (int f = 0; f < 4; ++f) {
                names.push_back("fx" + std::to_string(u) + "_" + fields[f]);
                const float mn = f == 1 ? .05f : f == 3 ? -.79f : 0.f, mx = f == 1 ? 7.f : f == 3 ? .79f : 1.f;
                t.push_back({names.back().c_str(), nullptr, mn, mx, f == 1, MVT_NONE, u, f});
            }
        return t;
    }();
    return table;
}

constexpr int MOD_MAX_CONNECTIONS = 32;

/** Resolved, audio-ready connection. */
struct ModConnection {
    int source = 0, layer = -1, target = -1, voice = MVT_NONE, curve = 0;
    float amount = 0.f;
    bool bipolar = false;
};
struct ModMatrixSnapshot {
    std::array<ModConnection, MOD_MAX_CONNECTIONS> list{};
    int count = 0;
};

inline float modNormalize(const ModTargetDef& d, float v) {
    if (d.log) return std::log(std::max(v, d.min) / d.min) / std::log(d.max / d.min);
    return (v - d.min) / (d.max - d.min);
}
inline float modDenormalize(const ModTargetDef& d, float n) {
    n = std::fmin(1.f, std::fmax(0.f, n));
    return d.log ? d.min * std::pow(d.max / d.min, n) : d.min + n * (d.max - d.min);
}
/** Source value after polarity and curve: -1..1 (bipolar) or 0..1. */
inline float modShape(float s, bool sourceBipolar, bool bipolar, int curve) {
    if (bipolar && !sourceBipolar) s = 2.f * s - 1.f;
    else if (!bipolar && sourceBipolar) s = .5f * (s + 1.f);
    const float a = std::fabs(s), sign = s < 0.f ? -1.f : 1.f;
    switch (curve) {
        case 1: return sign * a * a;                                  // exponential
        case 2: return sign * std::sqrt(a);                           // logarithmic
        case 3: return sign * a * a * (3.f - 2.f * a);                // S
        default: return s;
    }
}

/** Resolve "L<n>_<suffix>" against the target table; false if not modulatable. */
inline bool modResolveTarget(const std::string& id, int& layer, int& index) {
    if (id.size() < 4 || id[0] != 'L' || id[2] != '_' || id[1] < '1' || id[1] > '4') return false;
    layer = id[1] - '1';
    const std::string suffix = id.substr(3);
    const auto& t = modTargets();
    for (size_t i = 0; i < t.size(); ++i) if (suffix == t[i].suffix) { index = int(i); return true; }
    return false;
}

} // namespace SonusDSP
