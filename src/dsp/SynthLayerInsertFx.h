#pragma once
// Per-layer insert parameters (synth layers and, since v31, piano layers).
// Units: 0 Chorus I, 1 Chorus II, 2 Phaser I, 3 Phaser II, 4 Echo, 5 Reverb.
// The DSP chain lives in LayerInsertChain.h (it needs NordDspEffects.h, which
// itself includes NordTypes.h → this header).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "StageTempo.h"

namespace NordDSP {
struct SynthLayerFxParams {
    bool enabled = false;
    float rateHz = 0.55f;    // LFO frequency (chorus/phaser)
    float depth = 0.65f;     // modulation range
    float mix = 0.32f;       // wet/dry
    float feedback = 0.12f;  // regeneration (bounded)
    float timeMs = 210.f;    // echo time (or chorus base delay)
    bool syncEnabled = false; // Use SONUS masterBpm instead of timeMs
    int syncDivision = 5;    // quarter note by default
    // v31 reverb unit: 0 Room, 1 Plate, 2 Hall. Size uses `depth`.
    int type = 1;
    float damping = 0.45f;   // 0 bright .. 1 dark
    float preDelayMs = 12.f; // 0..250 ms
};

constexpr int INSERT_FX_UNITS = 6;
constexpr int INSERT_ECHO = 4;
constexpr int INSERT_REVERB = 5;
using InsertFxChain = std::array<SynthLayerFxParams, INSERT_FX_UNITS>;
/** True when at least one unit would change the signal. */
inline bool insertChainActive(const InsertFxChain& fx) {
    for (const auto& u : fx) if (u.enabled && u.mix > 0.0001f) return true;
    return false;
}
/** Seconds of silence before an idle chain stops processing (echo / reverb tails). */
inline float insertTailSeconds(const InsertFxChain& fx) {
    return (fx[INSERT_ECHO].enabled || fx[INSERT_REVERB].enabled) ? 9.f : 1.5f;
}

} // namespace NordDSP
