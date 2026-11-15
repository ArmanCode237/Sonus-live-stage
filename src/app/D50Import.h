#pragma once
// Roland D-50 patch banks (sysex DT1 dumps, e.g. the user's PN-D50-00 card) translated
// to the Live Stage synth: each D-50 partial becomes an LA32 layer (synthesis type 7)
// with its own TVF / TVA, as on the D-50. Synthesized partials are translated with the
// LA32 cutoff arithmetic documented by Munt (cutoff + 78 in 16ths of an octave above the
// note, key follow = TVF follow - pitch follow in 21sts, envelope depth = levelMult x
// level / 256). PCM partials need the D-50 wave ROM, which is not available: they are
// approximated by the engine's attack transients (one-shot waves) or by LA32 sustain
// partials (loop waves). Envelope times use an approximate time curve.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace D50 {

constexpr int PATCH_BYTES = 448;              // 2 tones x (2 partials + common) + patch block
using Patch = std::array<uint8_t, PATCH_BYTES>;

/** Parses a D-50 bulk dump (Roland DT1 messages, model 0x14) into its 64 patches. */
bool parseBank(const std::vector<uint8_t>& sysex, std::vector<Patch>& patches, std::string& error);
/** Patch name and the names of its upper / lower tones (D-50 character set). */
std::string patchName(const Patch& p);
std::string upperToneName(const Patch& p);
std::string lowerToneName(const Patch& p);
/** Key mode name: WHOLE, DUAL, SPLIT, SEPARATE, ... */
std::string keyModeName(const Patch& p);
/** Synth module parameters (the factory-preset JSON schema) approximating the patch. */
nlohmann::json toSynthParams(const Patch& p);

} // namespace D50
