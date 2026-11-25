#pragma once
// JSON for a per-layer insert chain (synth layers and piano layers). Shared by every
// serializer so the six units and their fields are written and read in one place.
// Older files with five units (no reverb) load unchanged: the reverb stays off.
#include "StageTypes.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace SonusDSP {

inline nlohmann::json insertFxToJson(const InsertFxChain& chain) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : chain)
        arr.push_back({{"enabled", f.enabled}, {"rateHz", f.rateHz}, {"depth", f.depth}, {"mix", f.mix},
                       {"feedback", f.feedback}, {"timeMs", f.timeMs}, {"syncEnabled", f.syncEnabled},
                       {"syncDivision", f.syncDivision}, {"type", f.type}, {"damping", f.damping},
                       {"preDelayMs", f.preDelayMs}});
    return arr;
}

inline void insertFxFromJson(const nlohmann::json& arr, InsertFxChain& chain) {
    if (!arr.is_array()) return;
    for (size_t e = 0; e < std::min<size_t>(INSERT_FX_UNITS, arr.size()); ++e) {
        if (!arr[e].is_object()) continue;
        const auto& v = arr[e];
        auto& f = chain[e];
        f.enabled = v.value("enabled", f.enabled);
        f.rateHz = std::clamp(v.value("rateHz", f.rateHz), .05f, 7.f);
        f.depth = std::clamp(v.value("depth", f.depth), 0.f, 1.f);
        f.mix = std::clamp(v.value("mix", f.mix), 0.f, 1.f);
        f.feedback = std::clamp(v.value("feedback", f.feedback), -.79f, .79f);
        f.timeMs = std::clamp(v.value("timeMs", f.timeMs), 35.f, 750.f);
        f.syncEnabled = v.value("syncEnabled", f.syncEnabled);
        f.syncDivision = std::clamp(v.value("syncDivision", f.syncDivision), 0, 15);
        f.type = std::clamp(v.value("type", f.type), 0, 2);
        f.damping = std::clamp(v.value("damping", f.damping), 0.f, 1.f);
        f.preDelayMs = std::clamp(v.value("preDelayMs", f.preDelayMs), 0.f, 250.f);
    }
}

} // namespace SonusDSP
