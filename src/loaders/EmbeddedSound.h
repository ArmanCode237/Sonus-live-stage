#pragma once
// v35: turns the per-region amplitude envelope / filter authored in an SF2 or SFZ file into
// one PianoInstrument::EmbeddedSound (the engine has one ENV and one filter per layer).
// Header-only; used by Sf2Loader and BankLoader (SFZ). Control thread only.
#include "../core/StageTypes.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace SonusDSP {

enum class EmbeddedTimeScale {
    // SF2 2.04: decay / release are the time of a linear-in-dB fall to -100 dB.
    Sf2,
    // SFZ: same convention as SF2 (sfizz / ARIA), but non-SF2 voices scale the layer
    // release by the stroke damper (0.25 s / 0.35 s reference, see StagePianoEngine).
    Sfz
};

inline float embeddedMedian(std::vector<float> v, float fallback) {
    if (v.empty()) return fallback;
    auto mid = v.begin() + std::ptrdiff_t(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return std::isfinite(*mid) ? *mid : fallback;
}

/** Which envelope stages the file actually writes (any region). Stages it leaves out keep
 *  the engine's defaults instead of the format's (an SF2 without a release generator would
 *  otherwise stop in 1 ms). */
struct EmbeddedAuthored { bool attack = false, decay = false, sustain = false, release = false;
    bool any() const { return attack || decay || sustain || release; } };

inline PianoInstrument::EmbeddedSound summariseEmbeddedSound(
        const EmbeddedAuthored& authored, const std::vector<float>& attack, const std::vector<float>& decay,
        const std::vector<float>& sustain, const std::vector<float>& release,
        const std::vector<float>& cutoffHz, const std::vector<float>& resonanceDb,
        EmbeddedTimeScale scale = EmbeddedTimeScale::Sf2) {
    PianoInstrument::EmbeddedSound e;
    if (authored.any() && !attack.empty()) {
        // Engine decay reaches -40 dB in decaySec, release reaches -60 dB in releaseSec:
        // keep the authored slope in dB/s.
        const float relToEngine = scale == EmbeddedTimeScale::Sfz ? 0.6f * (0.35f / 0.25f) : 0.6f;
        e.available = true;
        if (authored.attack) e.attackSec = std::clamp(embeddedMedian(attack, e.attackSec), 0.001f, 5.f);
        if (authored.sustain) e.sustainLevel = std::clamp(embeddedMedian(sustain, 1.f), 0.f, 1.f);
        // Full sustain: the decay stage is inaudible; keep the engine's neutral default.
        if (authored.decay && e.sustainLevel < 0.999f)
            e.decaySec = std::clamp(0.4f * embeddedMedian(decay, 0.f), 0.02f, 12.f);
        if (authored.release) e.releaseSec = std::clamp(relToEngine * embeddedMedian(release, 0.f), 0.02f, 10.f);
    }
    const float fc = embeddedMedian(cutoffHz, 20000.f);
    if (fc < 18000.f) {
        e.hasFilter = true;
        e.cutoffHz = std::clamp(fc, 40.f, 20000.f);
        const float db = std::clamp(embeddedMedian(resonanceDb, 0.f), 0.f, 40.f);
        e.resonanceQ = std::clamp(0.707f * std::pow(10.f, db / 20.f), 0.5f, 12.f);
    }
    return e;
}

} // namespace SonusDSP
