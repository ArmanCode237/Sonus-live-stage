#pragma once
// v31 synth fields (FM4, LA hybrid, attack transient, analog drift) shared by every
// JSON path: programs/Sounds (ParamSerialization) and module presets (WorkstationHost).
// Missing keys keep the value already in the config, so older files load unchanged.
#include "NordTypes.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace NordDSP {

inline void synthLayerExtrasToJson(const SynthLayerConfig& c, nlohmann::json& out) {
    out["analog"] = c.analog;
    out["fm4Algorithm"] = c.fm4Algorithm;
    out["fm4Feedback"] = c.fm4Feedback;
    out["fm4VelMod"] = c.fm4VelMod;
    out["opRatio"] = c.opRatio;
    out["opLevel"] = c.opLevel;
    out["opAttack"] = c.opAttack;
    out["opDecay"] = c.opDecay;
    out["opSustain"] = c.opSustain;
    out["laMix"] = c.laMix;
    out["laRing"] = c.laRing;
    out["transientType"] = c.transientType;
    out["transientLevel"] = c.transientLevel;
    out["transientDecay"] = c.transientDecay;
    out["transientTone"] = c.transientTone;
}

inline void synthLayerExtrasFromJson(const nlohmann::json& in, SynthLayerConfig& c) {
    if (!in.is_object()) return;
    auto readArray = [&in](const char* key, std::array<float,4>& dst, float lo, float hi) {
        if (!in.contains(key) || !in[key].is_array()) return;
        const auto& a = in[key];
        for (size_t i = 0; i < std::min<size_t>(4, a.size()); ++i)
            if (a[i].is_number()) dst[i] = std::clamp(a[i].get<float>(), lo, hi);
    };
    c.analog = std::clamp(in.value("analog", c.analog), 0.f, 1.f);
    c.fm4Algorithm = std::clamp(in.value("fm4Algorithm", c.fm4Algorithm), 0, 7);
    c.fm4Feedback = std::clamp(in.value("fm4Feedback", c.fm4Feedback), 0.f, 1.f);
    c.fm4VelMod = std::clamp(in.value("fm4VelMod", c.fm4VelMod), 0.f, 1.f);
    readArray("opRatio", c.opRatio, .5f, 16.f);
    readArray("opLevel", c.opLevel, 0.f, 1.f);
    readArray("opAttack", c.opAttack, .001f, 8.f);
    readArray("opDecay", c.opDecay, .01f, 20.f);
    readArray("opSustain", c.opSustain, 0.f, 1.f);
    c.laMix = std::clamp(in.value("laMix", c.laMix), 0.f, 1.f);
    c.laRing = std::clamp(in.value("laRing", c.laRing), 0.f, 1.f);
    c.transientType = std::clamp(in.value("transientType", c.transientType), 0, SYNTH_TRANSIENT_COUNT - 1);
    c.transientLevel = std::clamp(in.value("transientLevel", c.transientLevel), 0.f, 1.f);
    c.transientDecay = std::clamp(in.value("transientDecay", c.transientDecay), .005f, 4.f);
    c.transientTone = std::clamp(in.value("transientTone", c.transientTone), 0.f, 1.f);
}

} // namespace NordDSP
