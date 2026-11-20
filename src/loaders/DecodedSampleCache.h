#pragma once
#include "../core/StageTypes.h"
#include <filesystem>
#include <string>

namespace SonusDSP {
// Versioned, non-destructive PCM16 cache for NW1, SFZ/WAV and SF2 programs.
// NPNo retains its verified NPCACHE5 reader/writer. All methods are control-thread only.
class DecodedSampleCache {
public:
    static void setDirectory(const std::filesystem::path& directory);
    static std::filesystem::path directory();
    static bool supports(const std::string& path);
    static std::filesystem::path cachePath(const std::string& source, int sf2Preset=-1);
    static bool containsFresh(const std::string& source, int sf2Preset=-1);
    static bool load(const std::string& source, int sf2Preset, PianoInstrument& out);
    static bool store(const std::string& source, int sf2Preset, const PianoInstrument& bank);
    // Explicit library relocation only. Rebinds validated cache metadata when
    // original size and mtime are unchanged. SFZ dependencies require rebuild.
    static bool rebindRelocated(const std::string& oldSource,const std::string& newSource,int sf2Preset=-1);
};
}
