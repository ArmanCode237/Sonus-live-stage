#pragma once
// JSON <-> SynthPresetParams (programs.json, Sounds and module presets).
#include "app/AppCommon.h"
#include "modules/StagePianoEngine.h"

json serializeParams(const SynthPresetParams& p);
SynthPresetParams deserializeParams(const json& j, const SynthPresetParams& fallback);
