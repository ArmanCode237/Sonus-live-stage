#pragma once
// Reads the current value of a registered parameter ID from the serializeParams()
// snapshot (the same JSON the UI receives as /api/state.params).
// C++ port of webui/src/params/paramMap.ts; tests/v31_param_reader.cpp checks both
// against the same captured fixtures so they cannot drift apart.
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace SonusDSP {

std::optional<float> readParamValue(const nlohmann::json& params, const std::string& id);

/** Owning module of a parameter: 0 piano, 1 synth, 2 organ, -1 global (master, programs, master rack). */
int parameterModule(const std::string& id);

/** Piano layer whose Sound owns this parameter (sample settings, inserts, sends), or -1.
 *  Layer volume / on-off / zones belong to the Performance, not to the Sound. */
int soundLayerOfParam(const std::string& id);
/** The same Sound parameter on another piano layer ("pianoSample0Cut" → "pianoSample3Cut"). */
std::string retargetSoundParam(const std::string& id, int layer);

} // namespace SonusDSP
