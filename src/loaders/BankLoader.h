#pragma once
#include "NordTypes.h"
#include <string>
#include <filesystem>
#include <vector>


namespace NordDSP {

class BankLoader {
public:
    static bool loadAny(const std::string& pathStr, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadNpnoNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg,
                               bool skipCacheWrite = false);
    // Control-thread only. Allows InstrumentPool to persist PCM off the request path.
    static void setNpnoCacheDirectory(const std::filesystem::path& directory);
    static bool hasNpnoCache(const std::string& source);
    static bool rebindNpnoCache(const std::string& oldSource,const std::string& newSource);
    static void persistNpnoCache(const std::string& path,const PianoInstrument& instrument);
    static bool loadNpnoNativeShared(const std::string& path,const std::shared_ptr<PianoInstrument>& owner,std::string& status);
    static bool loadNsmpFallback(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);
    static bool loadFromDirectory(const std::string& folderPath, PianoInstrument& outInstrument);
    static bool loadFromSfz(const std::string& sfzFilePath, PianoInstrument& outInstrument);
    static bool loadClaviaProgramPreset(const std::string& filePath, PianoInstrument& ioInstrument, std::string& outStatusMsg);
    static ClaviaHeaderInfo inspectClaviaHeader(const std::string& filePath);
    static std::vector<LibraryEntry> scanLibraryDirectory(const std::string& rootDir);
    static bool loadNsmpNative(const std::string& filePath, PianoInstrument& outInstrument, std::string& outStatusMsg);

private:
    static void ensureSustainLoop(StrokeSample& s,bool progressive=false);
    static bool loadNpnoNativeImpl(const std::string& path,PianoInstrument& out,std::string& status,
                                   std::shared_ptr<PianoInstrument> owner,bool skipCacheWrite);
    static bool loadWaveFile(const std::string& wavPath, StrokeSample& s);
    static void loadWavesInParallel(std::vector<StrokeSample>& strokes, const std::vector<std::string>& wavPaths);
};

} // namespace NordDSP