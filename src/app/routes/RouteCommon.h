#pragma once
// Common includes for the HTTP route translation units.
#include "app/StageApp.h"
#include "app/AudioBackend.h"
#include "core/LibraryPathRelocation.h"
#include "loaders/BankLoader.h"
#include "loaders/DecodedSampleCache.h"
#include "loaders/InstrumentPool.h"
#include "loaders/SampleLibraryManager.h"
#include "loaders/Sf2Loader.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

/** Name stored in selectedSoundRefs[key] ({name, kind}), or "" when it is null / missing. */
inline std::string presetRefName(const nlohmann::json& refs, const char* key) {
    if (!refs.is_object() || !refs.contains(key) || !refs[key].is_object()) return {};
    const auto& ref = refs[key];
    return ref.contains("name") && ref["name"].is_string() ? ref["name"].get<std::string>() : std::string();
}
