#pragma once
// Build edition: FULL (default) or DEMO (cmake -DSTAGE_DEMO=ON).
// Every demo limit lives here; the rest of the code only asks these constants,
// so the full build is unchanged (all checks fold to "allowed" at compile time).
#include <cstring>
#include <string>

namespace SonusDSP::Edition {

#if defined(STAGE_DEMO) && STAGE_DEMO
inline constexpr bool kDemo = true;
#else
inline constexpr bool kDemo = false;
#endif

inline constexpr const char* kName = kDemo ? "demo" : "full";

/** Piano layers that may play / load a sound (the full build has PIANO_LAYER_COUNT). */
inline constexpr int kMaxPianoLayers = kDemo ? 2 : 1 << 30;
/** Saving programs, performances, Sounds and module presets (and the session autosave). */
inline constexpr bool kCanSave = !kDemo;
/** MIDI Learn / mapping edits and the OSC server. */
inline constexpr bool kCanMap = !kDemo;

inline constexpr bool pianoLayerAllowed(int layer) { return layer < kMaxPianoLayers; }

inline constexpr const char* kLockedMessage =
    "Función no disponible en la versión DEMO de Sonus Live Stage";

/** HTTP routes the demo rejects (POST only). Read in the shared request guard. */
inline bool routeBlocked(const std::string& method, const std::string& path) {
    if (!kDemo || method != "POST") return false;
    static const char* const saveRoutes[] = {
        "/api/programs/store", "/api/programs/import", "/api/performances/store",
        "/api/piano/performances/store", "/api/piano/presets/save", "/api/sounds/save",
        "/api/synth/presets/save", "/api/synth/factory/save", "/api/organ/presets/save",
    };
    static const char* const mapRoutes[] = {"/api/mappings", "/api/osc", "/api/osc/test"};
    if (!kCanSave) for (const char* r : saveRoutes) if (path == r) return true;
    if (!kCanMap) for (const char* r : mapRoutes) if (path == r) return true;
    return false;
}

}  // namespace SonusDSP::Edition
